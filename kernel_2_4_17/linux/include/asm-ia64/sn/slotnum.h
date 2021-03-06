/* $Id: slotnum.h,v 1.1.1.1 2004/02/09 13:44:54 cmo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SLOTNUM_H
#define _ASM_SN_SLOTNUM_H

#include <linux/config.h>

typedef	unsigned char slotid_t;

#if defined (CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sn1/slotnum.h>
#else

#error <<BOMB! slotnum defined only for SN0 and SN1 >>

#endif /* !CONFIG_SGI_IP35 && !CONFIG_IA64_SGI_SN1 */

#endif /* _ASM_SN_SLOTNUM_H */
