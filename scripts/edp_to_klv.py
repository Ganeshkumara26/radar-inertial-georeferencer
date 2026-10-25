import struct
import json
import argparse
import sys
import os

# EDP Frame Format
# Header: 
# uint8_t  magic (0xED)
# uint8_t  version
# uint16_t length
# uint32_t sequence
# uint32_t timestamp_us
# uint8_t  target_count
# uint8_t  status
HEADER_FMT = "<BBHIIBB"
HEADER_SIZE = 14

# Target Record (40 bytes based on types, though macro said 32)
# double latitude, double longitude, float altitude, float vel_east, float vel_north, float vel_up, float snr, uint32_t timestamp_us
TARGET_FMT = "<ddfffffI"
TARGET_SIZE = 40

# Custom KLV Tags
TAG_UNIX_TIMESTAMP = 2
TAG_SENSOR_LATITUDE = 13
TAG_SENSOR_LONGITUDE = 14
TAG_SENSOR_TRUE_ALTITUDE = 15
TAG_TARGET_LOCATION_LATITUDE = 40
TAG_TARGET_LOCATION_LONGITUDE = 41
TAG_TARGET_LOCATION_ELEVATION = 42
TAG_ABSOLUTE_TIMESTAMP = 94
TAG_RF_BAND_ID = 95

def parse_edp_stream(binary_data):
    frames = []
    offset = 0
    
    while offset + HEADER_SIZE <= len(binary_data):
        magic = binary_data[offset]
        if magic != 0xED:
            offset += 1
            continue
            
        header_data = binary_data[offset:offset+HEADER_SIZE]
        magic, version, length, sequence, timestamp_us, target_count, status = struct.unpack(HEADER_FMT, header_data)
        
        if offset + length > len(binary_data):
            break # Incomplete frame
            
        targets = []
        target_offset = offset + HEADER_SIZE
        
        # We try to parse according to TARGET_FMT (40 bytes)
        for _ in range(target_count):
            if target_offset + TARGET_SIZE > offset + length - 4:
                break
            target_data = binary_data[target_offset:target_offset+TARGET_SIZE]
            lat, lon, alt, ve, vn, vu, snr, ts = struct.unpack(TARGET_FMT, target_data)
            targets.append({
                "latitude": lat,
                "longitude": lon,
                "altitude": alt,
                "velocity_east": ve,
                "velocity_north": vn,
                "velocity_up": vu,
                "snr": snr,
                "timestamp_us": ts
            })
            target_offset += TARGET_SIZE
            
        # Parse CRC32
        crc_data = binary_data[offset+length-4:offset+length]
        crc, = struct.unpack("<I", crc_data) if len(crc_data) == 4 else (0,)
        
        frames.append({
            "header": {
                "magic": hex(magic),
                "version": version,
                "length": length,
                "sequence": sequence,
                "timestamp_us": timestamp_us,
                "target_count": target_count,
                "status": status
            },
            "targets": targets,
            "crc32": hex(crc)
        })
        
        offset += length
        
    return frames

def encode_length(length):
    if length < 128:
        return bytes([length])
    elif length <= 0xFF:
        return bytes([0x81, length])
    elif length <= 0xFFFF:
        return struct.pack(">BH", 0x82, length)
    else:
        return struct.pack(">BI", 0x84, length)

def encode_klv_tag(tag, value_bytes):
    return bytes([tag]) + encode_length(len(value_bytes)) + value_bytes

def convert_to_klv(frames, rf_band_id=1):
    klv_stream = bytearray()
    
    # 16-byte Universal Key for MISB ST 0601 Local Set
    universal_key = bytes.fromhex("06 0E 2B 34 02 0B 01 01 0E 01 03 01 01 00 00 00")
    
    for frame in frames:
        local_set = bytearray()
        
        # Unix Timestamp (Tag 2) - using frame timestamp as relative offset
        local_set += encode_klv_tag(TAG_UNIX_TIMESTAMP, struct.pack(">Q", frame["header"]["timestamp_us"]))
        
        # Absolute Timestamp (Custom Tag 94)
        local_set += encode_klv_tag(TAG_ABSOLUTE_TIMESTAMP, struct.pack(">Q", frame["header"]["timestamp_us"]))
        
        # RF Band ID (Custom Tag 95)
        local_set += encode_klv_tag(TAG_RF_BAND_ID, bytes([rf_band_id]))
        
        # Encode targets
        for t in frame["targets"]:
            # Target Latitude (Tag 40) - mapped to int32 range
            lat_int = int((t["latitude"] / 90.0) * (2**31 - 1))
            local_set += encode_klv_tag(TAG_TARGET_LOCATION_LATITUDE, struct.pack(">i", lat_int))
            
            # Target Longitude (Tag 41)
            lon_int = int((t["longitude"] / 180.0) * (2**31 - 1))
            local_set += encode_klv_tag(TAG_TARGET_LOCATION_LONGITUDE, struct.pack(">i", lon_int))
            
            # Target Elevation (Tag 42)
            alt_int = max(0, min(65535, int(t["altitude"])))
            local_set += encode_klv_tag(TAG_TARGET_LOCATION_ELEVATION, struct.pack(">H", alt_int))
            
        # Wrap local set with Universal Key and length
        klv_stream += universal_key + encode_length(len(local_set)) + local_set
        
    return klv_stream

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EDP to JSON/KLV Bridge")
    parser.add_argument("--input", "-i", required=True, help="Input binary EDP stream file")
    parser.add_argument("--json", "-j", help="Output JSON file")
    parser.add_argument("--klv", "-k", help="Output KLV binary file")
    parser.add_argument("--rf-band", type=int, default=1, help="RF Band ID (1=30GHz, 2=433MHz)")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input file {args.input} not found.")
        sys.exit(1)
        
    with open(args.input, "rb") as f:
        binary_data = f.read()
        
    frames = parse_edp_stream(binary_data)
    print(f"Parsed {len(frames)} EDP frames.")
    
    if args.json:
        with open(args.json, "w") as f:
            json.dump(frames, f, indent=2)
        print(f"Saved JSON to {args.json}")
        
    if args.klv:
        klv_data = convert_to_klv(frames, args.rf_band)
        with open(args.klv, "wb") as f:
            f.write(klv_data)
        print(f"Saved KLV stream to {args.klv}")
