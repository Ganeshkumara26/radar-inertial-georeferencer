CC = gcc
CFLAGS = -Wall -Wextra -g3 -O0 -I.. -I../platform -I../src -I. -std=c11 -DSIMULATION_BUILD

TESTS = test_ekf test_ring_buffer test_mavlink

all: $(TESTS)

test_ekf: ekf/test_ekf.c ../src/ekf.c ../src/ekf.h ../platform/platform.h platform_stub.c
	$(CC) $(CFLAGS) ekf/test_ekf.c ../src/ekf.c platform_stub.c -lm -o test_ekf

test_ring_buffer: ring_buffer/test_ring_buffer.c ../src/ring_buffer.c ../src/ring_buffer.h ../platform/platform.h platform_stub.c
	$(CC) $(CFLAGS) ring_buffer/test_ring_buffer.c ../src/ring_buffer.c platform_stub.c -lm -o test_ring_buffer

test_mavlink: mavlink/test_mavlink.c ../src/mavlink_parser.c ../src/mavlink_parser.h ../platform/platform.h platform_stub.c
	$(CC) $(CFLAGS) mavlink/test_mavlink.c ../src/mavlink_parser.c platform_stub.c -lm -o test_mavlink

run: all
	@echo "=== Running EKF tests ==="
	@./test_ekf
	@echo ""
	@echo "=== Running Ring Buffer tests ==="
	@./test_ring_buffer
	@echo ""
	@echo "=== Running MAVLink Parser tests ==="
	@./test_mavlink

clean:
	rm -f $(TESTS)

.PHONY: all clean run
