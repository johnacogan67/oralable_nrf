#include <zephyr/init.h>

#if defined(CONFIG_USE_SEGGER_RTT)
#include <SEGGER_RTT.h>

/*
 * Force RTT control block initialization as early as possible.
 *
 * This makes JLinkRTTLogger able to attach even if later boot code crashes
 * before the normal logging backend is fully online.
 */
static int oralable_early_rtt_init(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_WriteString(0, "[EARLY] RTT initialized\n");
    return 0;
}

SYS_INIT(oralable_early_rtt_init, PRE_KERNEL_1, 0);
#endif

