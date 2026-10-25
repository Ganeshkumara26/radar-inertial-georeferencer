/* Platform stubs for host-based testing */
#include "platform.h"
#include <string.h>

static uint32_t stub_dma_available = 0;
static const uint8_t *stub_dma_buffer = NULL;

void platform_set_dma_stub(uint32_t available, const uint8_t *buffer) {
    stub_dma_available = available;
    stub_dma_buffer = buffer;
}

void platform_init(void) {}
void platform_early_init(void) {}
void platform_timer_init(void) {}
uint32_t platform_timer_get_us(void) { return 0; }
void platform_timer_frame_sync_handler(void) {}
void platform_uart_init(void) {}
void platform_uart_putchar(char c) { (void)c; }
void platform_uart_puts(const char *s) { while (*s) platform_uart_putchar(*s++); }
void platform_dma_init(void) {}
uint32_t platform_dma_rx_available(void) { return stub_dma_available; }
const uint8_t *platform_dma_rx_buffer(void) { return stub_dma_buffer; }
void platform_dma_rx_ack(uint32_t count) { (void)count; }
void platform_crc32_init(void) {}
uint32_t platform_crc32_compute(const uint8_t *data, uint32_t len) { (void)data; (void)len; return 0; }
void platform_irq_enable(void) {}
void platform_irq_disable(void) {}
void platform_memory_barrier(void) {}
#ifdef SIMULATION_BUILD
void platform_sim_frame_sync_trigger(void) {}
void platform_sim_radar_trigger(void) {}
#endif
