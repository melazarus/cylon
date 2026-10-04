#include <ch32x035.h> /* both X033 and X035 */

#include <wch_usbmidi_internal.h>

#include "anim.h"
#include "board.h"
#include "debug.h"
#include "midi.h"

int main(void)
{
    SystemInit();
#ifdef NVIC_PriorityGroup_2
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
#else
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
#endif
    SystemCoreClockUpdate();
    Delay_Init();

#if DEBUG
    /* serial logging for the debug build (USART1 TX = PB10, 115200 8N1) */
    USART_Printf_Init(115200);
#endif

    /* bring up the LED driver and hold the data line low right away; the LEDs
     * show an undefined color until they receive their first valid frame */
    board_init();
    anim_init();

    PRINT("SystemClk: %u\r\n", (unsigned)SystemCoreClock);
    PRINT("ChipID: %08x\r\n", (unsigned)DBGMCU_GetCHIPID());

    
    /* Startup animation, will change in the future when there is a state we can persist.
     */
    for (int i = 0; i < 30; i++)
    {
        board_set_led(0, i*8, 0x00, (29*8)-(i*8));
        board_set_led(1, (29*8)-(i*8), i*8, 0x00);
        board_set_led(2, 0x00, (29*8)-(i*8), i*8);

        board_show_leds();
        Delay_Ms(100);
    }

    USB_init();

    PRINT("Cylon init done\r\n");

    /* USB-MIDI LED controller mode, never returns */
    midi_run();
}
