/*
 *  WDT interface for ALi M6117 SoC
 *
 *  Author: Andrea Galbusera <gizero@gmail.com>
 *
 *  Based on previous works by Federico Bareilles <fede@fcaglp.unlp.edu.ar>,
 *  Instituto Argentino de Radio Astronomia (IAR).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * TODO:
 * - nowayout parameter is useless: config option always win at moment
 * - 
 */

#include <linux/module.h>
#include <linux/io.h>

#include <linux/watchdog.h>

#define DRIVER_NAME		"alim6117-wdt"

/* Port definitions: */
#define M6117_PORT_INDEX 0x22
#define M6117_PORT_DATA  0x23
/* YES, the two unused ports of 8259:
 * 0020-003f : pic1 
 *
 * The 8259 Interrup Controller uses four port addresses (0x20 through
 * 0x23). Although IBM documentation indicates that these four port
 * addresses are reserved for the 8259, only the two lower ports (0x20
 * and 0x21) ar documented as usable by programers. The two ports
 * (0x22 and 0x23) are used only when reprogramming the 8259 for
 * special dedicated systems that operate in modes which are not
 * compatible with normal IBM PC operation (this case).
 **/

/* Index for ALI M6117: */
#define ALI_LOCK_REGISTER 0x13
#define ALI_WDT           0x37
#define ALI_WDT_SELECT    0x38
#define ALI_WDT_DATA0     0x39
#define ALI_WDT_DATA1     0x3a
#define ALI_WDT_DATA2     0x3b
#define ALI_WDT_CTRL      0x3c

/* Time out generates signal select: */
#define WDT_SIGNAL_IRQ3  0x10
#define WDT_SIGNAL_IRQ4  0x20
#define WDT_SIGNAL_IRQ5  0x30
#define WDT_SIGNAL_IRQ6  0x40
#define WDT_SIGNAL_IRQ7  0x50
#define WDT_SIGNAL_IRQ9  0x60
#define WDT_SIGNAL_IRQ10 0x70
#define WDT_SIGNAL_IRQ11 0x80
#define WDT_SIGNAL_IRQ12 0x90
#define WDT_SIGNAL_IRQ14 0xa0
#define WDT_SIGNAL_IRQ15 0xb0
#define WDT_SIGNAL_NMI   0xc0
#define WDT_SIGNAL_SRSET 0xd0
/* set signal to use: */
#define WDT_SIGNAL       WDT_SIGNAL_SRSET

#define WATCHDOG_MINTIMEOUT 1
#define WATCHDOG_MAXTIMEOUT 512
#define WATCHDOG_DEFAULT_TIMEOUT 60	/* 60 sec default timeout */

/* module parameters */
static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int timeout = WATCHDOG_DEFAULT_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds. (1 < timeout < 512, default="
				__MODULE_STRING(WATCHDOG_DEFAULT_TIMEOUT) ")");

static u8 alim6117_read(u8 index)
{
	outb(index, M6117_PORT_INDEX);
	return inb(M6117_PORT_DATA);
}

static void alim6117_write(u8 index, u8 data)
{
	outb(index, M6117_PORT_INDEX);
	outb(data, M6117_PORT_DATA);
}

static void alim6117_dump_regs(void)
{
	u8 reg1, reg2, reg3, reg4, reg5, reg6, reg7;
	
	reg1 = alim6117_read(ALI_LOCK_REGISTER);
	reg2 = alim6117_read(ALI_WDT);
	reg3 = alim6117_read(ALI_WDT_CTRL);
	reg4 = alim6117_read(ALI_WDT_SELECT);
	reg5 = alim6117_read(ALI_WDT_DATA2);
	reg6 = alim6117_read(ALI_WDT_DATA1);
	reg7 = alim6117_read(ALI_WDT_DATA0);
	
	pr_debug("Register dump: 0x13=0x%02x, 0x37=0x%02x, 0x3c=0x%02x, 0x38=0x%02x\n",
		reg1, reg2, reg3, reg4);
	pr_debug("Register dump: 0x3b=0x%02x, 0x3a=0x%02x, 0x39=0x%02x\n",
		reg5, reg6, reg7);
}

static void alim6117_cfg_unlock(void)
{
	pr_debug("Unlock config registers\n");
	alim6117_write(ALI_LOCK_REGISTER, 0xc5);
	alim6117_dump_regs();
}

static void alim6117_cfg_lock(void)
{
	pr_debug("Lock config registers\n");
	alim6117_dump_regs();
	alim6117_write(ALI_LOCK_REGISTER, 0x00);
}

/*
Index Port 37h
Bit 7   Reserved.
Bit 6   0: Disable WDT0
        1: Enable WDT0 (default)
Bit 5-0 Reserved.
*/
static void alim6117_wdt_disable(void)
{
	u8 val = alim6117_read(ALI_WDT);

	val &= ~0x40;
	alim6117_write(ALI_WDT, val);
}

static void alim6117_wdt_enable(void)
{
	u8 val = alim6117_read(ALI_WDT);

	val |= 0x40;
	alim6117_write(ALI_WDT, val);
}

/*
Index Port 38h
Bit 7-4   Reserved
Bit 3-0   0000:Reserved
          0001:IRQ3
          0010:IRQ4
          0011:IRQ5
          0100:IRQ6
          0101:IRQ7
          0110:IRQ9
          0111:IRQ10
          1001:IRQ12
          1010:IRQ14
          1011:IRQ15
          1100:NMI
          1101:System reset
          1110:Reserved
          1111:Reserved
*/
static void alim6117_wdt_signal_select(u8 signal)
{
	u8 val = alim6117_read(ALI_WDT_SELECT);

	val &= 0xf0;
	val |= signal;
	alim6117_write(ALI_WDT_SELECT, val);
}

/*
Index 3Bh, 3Ah, 39h : Counter
   3Bh           3Ah           39h
D7......D0    D7......D0    D7......D0

                Counter
  MSB...........................LSB
*/
static void alim6117_wdt_set_counter(unsigned int t)
{
	unsigned int tmrval;
	
	tmrval = 0x8000 * t;
	
	pr_debug("%s: t: %d tmrval: %d\n", __func__, t, tmrval);
	
	alim6117_write(ALI_WDT_DATA2, (tmrval >> 16) & 0xff);
	alim6117_write(ALI_WDT_DATA1, (tmrval >> 8) & 0xff);
	alim6117_write(ALI_WDT_DATA0, (tmrval >> 0) & 0xff);
}

/*
Index Port 3Ch
Bit 7   0: Read only, Watchdog timer time out event does not happen.
        1: Read only, Watchdog timer time out event happens.
Bit 6   Write 1 to reset Watchdog timer.
*/
static void alim6117_wdt_reset(void)
{
	u8 val = alim6117_read(ALI_WDT_CTRL);

	val |= 0x40;
	alim6117_write(ALI_WDT_CTRL, val);
}

static int alim6117_wdt_set_timeout(struct watchdog_device *wd_dev, unsigned int t)
{
	alim6117_cfg_unlock();
	alim6117_wdt_set_counter(t);
	alim6117_cfg_lock();

	return 0;
}

static int alim6117_wdt_start(struct watchdog_device *wd_dev)
{
	alim6117_cfg_unlock();
	alim6117_wdt_disable();
	alim6117_wdt_set_counter(timeout);
	alim6117_wdt_signal_select(WDT_SIGNAL);
	alim6117_wdt_enable();
	alim6117_cfg_lock();

	return 0;
}

static int alim6117_wdt_stop(struct watchdog_device *wd_dev)
{
	alim6117_cfg_unlock();
	alim6117_wdt_disable();
	alim6117_cfg_lock();

	return 0;
}

static int alim6117_wdt_ping(struct watchdog_device *wd_dev)
{
	alim6117_cfg_unlock();
	alim6117_wdt_reset();
	alim6117_cfg_lock();

	return 0;
}

static const struct watchdog_info alim6117_ident = {
	.options =		WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
	.firmware_version =	0,
	.identity =		DRIVER_NAME,
};

static const struct watchdog_ops alim6117_wdt_ops = {
	.owner =		THIS_MODULE,
	.start =		alim6117_wdt_start,
	.stop = 		alim6117_wdt_stop,
	.ping = 		alim6117_wdt_ping,
	.set_timeout =	alim6117_wdt_set_timeout,
};

static struct watchdog_device alim6117_wdt_watchdog_dev = {
	.info =		&alim6117_ident,
	.ops = 		&alim6117_wdt_ops,
	.min_timeout = WATCHDOG_MINTIMEOUT,
	.max_timeout = WATCHDOG_MAXTIMEOUT,
};

static int __init alim6117_wdt_init(void)
{
	if (timeout < WATCHDOG_MINTIMEOUT ||
	    timeout > WATCHDOG_MAXTIMEOUT) {
	    	pr_info("%s: Watchdog timeout out of range. Using default value.\n", __func__);
	    	timeout = WATCHDOG_DEFAULT_TIMEOUT;
	    }
	
	return watchdog_register_device(&alim6117_wdt_watchdog_dev);
}

static void __exit alim6117_wdt_exit(void)
{
	watchdog_unregister_device(&alim6117_wdt_watchdog_dev);
}

module_init(alim6117_wdt_init);
module_exit(alim6117_wdt_exit);

MODULE_AUTHOR("Andrea Galbusera <gizero@gmail.com>");
MODULE_DESCRIPTION("WDT interface for ALi M6117 SoC");
MODULE_LICENSE("GPL");

