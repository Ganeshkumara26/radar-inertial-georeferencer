# Protocol

The telemetry protocol is a simplified MAVLink v1/v2-inspired binary packet format. Nothing too fancy — just enough to exercise the parser and stress-test the edge cases.

## Packet Structure

```mermaid
flowchart LR
    subgraph Packet["MAVLink-style Telemetry Packet"]
        MAGIC["Magic: 0xFE<br/>(1 byte)"]
        LENGTH["Length: N<br/>(1 byte)"]
        SEQ["Sequence<br/>(1 byte)"]
        SYSID["System ID<br/>(1 byte)"]
        COMPID["Component ID<br/>(1 byte)"]
        MSGID["Message ID<br/>(1 byte)"]
        PAYLOAD["Payload: N bytes<br/>(roll, pitch, yaw floats)"]
        CRC16["CRC-16 CCITT<br/>(2 bytes)"]
    end
    MAGIC --> LENGTH --> SEQ --> SYSID --> COMPID --> MSGID --> PAYLOAD --> CRC16

    style MAGIC fill:#e3f2fd
    style LENGTH fill:#e3f2fd
    style SEQ fill:#e8f5e7
    style SYSID fill:#e8f5e7
    style COMPID fill:#e8f5e7
    style MSGID fill:#fce4ec
    style PAYLOAD fill:#fff3e0
    style CRC16 fill:#ffebee
```

## Key Design Decisions

### Struct Packing

```mermaid
flowchart TD
    subgraph Unpacked["Without __attribute__((packed))"]
        U1["Header: 6 bytes"] --> U2["Padding: 2 bytes"] --> U3["Payload: 12 bytes (3 floats)"]
        U4["Problem: 2 extra bytes<br/>Wire doesn't match struct layout"]
    end

    subgraph Packed["With __attribute__((packed))"]
        P1["Header: 6 bytes"] --> P2["Payload: 12 bytes (3 floats)"] --> P3["Total: 18 bytes"]
        P4["Match: struct == wire bytes"]
    end

    style Unpacked fill:#ffebee,color:#000
    style Packed fill:#e8f5e7,color:#000
```

The ARM Cortex-M7 is 32-bit and GCC naturally aligns `float` fields to 4-byte boundaries. Our 6-byte header means the compiler injects 2 bytes of invisible padding before the first `float`. We fixed this with `__attribute__((packed))` in v2.

### Data Flow

```mermaid
sequenceDiagram
    participant W as Wire (UART RX)
    participant ISR as USART ISR
    participant R as Ring Buffer
    participant P as Parser State Machine
    participant S as Struct
    participant C as CRC Check

    W->>ISR: Byte received (interrupt)
    ISR->>R: Push byte into ring buffer
    R->>P: Parser pulls bytes
    P->>P: Parse header byte-by-byte
    P->>S: Copy payload into packed struct
    S->>C: CRC-16 over header+payload
    C->>C: Compare against received CRC
    C->>P: Match = valid packet
```

### Endianness Collision (v4)

The Cortex-M7 is little-endian, but the hardware CRC peripheral processes words MSB-first. Without `REV_IN`, a 32-bit write of `0x34333231` feeds the wrong byte order into the CRC engine, producing wrong checksums. The fix was enabling `USART_CR1_RE` ... no, actually enabling `CRC_CR_REV_IN` in the CRC peripheral config register.
