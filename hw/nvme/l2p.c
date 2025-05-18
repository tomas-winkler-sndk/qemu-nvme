#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "system/system.h"
#include "system/block-backend.h"

#include "nvme.h"
#include "trace.h"

void test(void);

typedef struct _nvme_flash_address_params {
	uint32_t num_of_entries;           /* num of fa descriptors */
	uint32_t flash_address_entry_size; /* in bytes */
	uint32_t flash_address_page_size;  /* in bytes */
} NvmeFlashAddressParams;

typedef struct nvme_flash_address_page_dsc {
	uint32_t page_version;
	uint32_t reserved;
	uint32_t page_address_low;
	uint32_t page_address_high;
} NvmeFlashAaddressPageDsc;

typedef struct nvme_flash_address_page_array {
	NvmeFlashAddressParams params;
	NvmeFlashAaddressPageDsc dscs[];
} NvmeFlashAddressDescsArray;

/*
struct flash_address_page_buf {
}
*/

#if 0
static uint16_t nvme_fa_init_params(NvmeCtrl *ctrl,
				    uint32_t num_of_entries,
				    uint32_t fa_entry_size, uint32_t fa_page_size)
{
        const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
        dma_addr_t residual;
	NvmeFlashAddressParams params;

	params.num_of_entries = num_of_entries;
	params.flash_address_entry_size = fa_entry_size;
	params.flash_address_page_size = fa_page_size;

	dma_buf_write(&params, sizeof(params), &residual, &ctrl->hmb.sg, attrs);

        if (unlikely(residual)) {
            trace_pci_nvme_err_invalid_dma();
            return NVME_INVALID_FIELD | NVME_DNR;
        }

    return NVME_SUCCESS;
}
static bool l2p_set_lba_location(uint64_t lba)
{
	return true;
}
#endif /* 0 */

/* Find descriptor index */
static inline uint64_t lba_to_fa_desc_index(NvmeFlashAddressParams *params, uint64_t lba)
{
	return lba / params->num_of_entries;
}

static inline uint64_t lba_to_fa_page_index(NvmeFlashAddressParams *params, uint64_t lba)
{
	return lba % (params->flash_address_page_size / params->flash_address_entry_size);
}

static uint64_t l2p_get_lba_location(NvmeFlashAddressDescsArray *dsc_array, uint64_t lba)
{
	uint64_t dsc_index = lba_to_fa_desc_index(&dsc_array->params, lba);
	NvmeFlashAaddressPageDsc *page_dsc = &dsc_array->dscs[dsc_index];
	uint64_t page_idx = lba_to_fa_page_index(&dsc_array->params, lba);

	if (page_dsc->page_version == (uint32_t)-1)
		return 0;

	/* fix me */
	if (page_idx == 0)
		return 0;
	return lba;
}

void test(void)
{
	uint64_t lba = 0;
	NvmeFlashAddressDescsArray *dsc_array = g_malloc0(sizeof(NvmeFlashAddressDescsArray) + 10 * sizeof(NvmeFlashAaddressPageDsc));

	l2p_get_lba_location(dsc_array, lba);
	g_free(dsc_array);
}
