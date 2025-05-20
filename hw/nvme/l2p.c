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


typedef struct nvme_flash_address_page_dsc {
    uint32_t page_version;
    uint32_t segment_index;
    uint32_t page_address_low;
    uint32_t page_address_high;
} NvmeFlashAaddressPageDsc;

typedef struct nvme_flash_address_page_array {
    NvmeFlashAddressMapParams params;
    NvmeFlashAaddressPageDsc dscs[];
} NvmeFlashAddressDescsArray;

/* Calculate FlashAddressMap required Sets*/
static uint32_t nvme_ns_fam_calc_sets(NvmeNamespace *ns,
                                      uint32_t fam_set_size,
                                      uint32_t addr_size, Error **errp)
{
    uint64_t nlbas = ns->size / (ns->lbasz + ns->lbaf.ms);
    uint32_t lbas_per_set = fam_set_size / addr_size;
    if (lbas_per_set == 0) {
            error_setg(errp, "wrong fam_set size %"PRIu32" or addr size %"PRIu32"",
               fam_set_size, addr_size);
        return (uint32_t)-1;
    }
    return QEMU_ALIGN_UP(nlbas, lbas_per_set);
}

/* Calculate FlashAddressMap required memory */
size_t nvme_ns_fam_calc_required_mem(NvmeNamespace *ns,
                                     uint32_t fam_set_size, uint32_t addr_size, Error **errp)
{

    uint32_t num_sets = nvme_ns_fam_calc_sets(ns, fam_set_size, addr_size, errp);
    size_t mem_size = sizeof(NvmeFlashAddressMapParams) + num_sets * sizeof(NvmeFlashAaddressPageDsc);

    return MAX(QEMU_ALIGN_UP(mem_size, NVME_HMB_UNIT_SZ), fam_set_size);
}

void nvme_ns_fam_setup_params(NvmeNamespace *ns, uint32_t fam_set_size, uint32_t addr_size)
{
    NvmeFlashAddressMapParams *params = &ns->fam_params;
    Error *err = NULL;
    params->num_of_entries = nvme_ns_fam_calc_sets(ns, fam_set_size, addr_size, &err);
    params->entry_size = addr_size;
    params->set_size = fam_set_size;
    params->segment_index = 0;
}

static uint16_t nvme_ns_fam_update_buffer(NvmeCtrl *ctrl, void *buf, size_t buf_sz)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    dma_addr_t residual;

    dma_buf_write(buf, buf_sz, &residual, &ctrl->hmb.sg, attrs);

    if (unlikely(residual)) {
        trace_pci_nvme_err_invalid_dma();
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static void nvme_ns_fam_setup(NvmeNamespace *ns)
{

    NvmeCtrl *ctrl = ns->ctrl;

    if (ctrl == NULL)  {
        return;
    }

    nvme_ns_fam_setup_params(ns, NVME_FLASH_ADDRES_SET_SZ, NVME_HMB_ADDRES_SZ);

    Error *err = NULL;
    NvmeFlashAddressMapParams *params = &ns->fam_params;
    uint32_t count = ns->ctrl->hmb.hmb_count;
    QEMUSGList *sgl = &ns->ctrl->hmb.sg;
    uint32_t fam_set_size = params->set_size;
    uint64_t fam_offset = 0;
    uint32_t fam_index = 0;

    size_t total = nvme_ns_fam_calc_required_mem(ns, fam_set_size, params->entry_size, &err);
    NvmeFlashAddressDescsArray *fa_array = g_malloc0(total);
    memcpy(fa_array, params, sizeof(*params));

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t sets_in_current_chunk = (uint32_t)(sgl->sg[i].len / fam_set_size);
        fam_offset = 0;
        for (uint32_t j = 0; j < sets_in_current_chunk; ++j) {
                fam_offset = fam_offset +  j * fam_set_size;
                fa_array->dscs[fam_index].page_address_low = lo32(fam_offset);
                fa_array->dscs[fam_index].page_address_high = up32(fam_offset);
                fa_array->dscs[fam_index].page_version = 0;
                fa_array->dscs[fam_index].segment_index = i;
                fam_index++;
        }
    }

    nvme_ns_fam_update_buffer(ctrl, params, sizeof(*params));

    g_free(fa_array);

}

void nvme_ns_fam_setup_all(NvmeCtrl *n)
{
    for (uint32_t nsid = 1; nsid <= NVME_MAX_NAMESPACES; nsid++) {
        NvmeNamespace *ns = nvme_ns(n, nsid);
        if (ns) {
            nvme_ns_fam_setup(ns);
        }
    }
}



#if 0
static uint16_t nvme_fa_init_params(NvmeCtrl *ctrl,
                                uint32_t num_of_entries,
                    uint32_t fa_entry_size, uint32_t fa_page_size)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    dma_addr_t residual;
    NvmeFlashAddressMapParams params;

    params.num_of_entries = num_of_entries;
    params.flash_address_entry_size = fa_entry_size;
    params.flash_address_set_size = fa_page_size;

    dma_buf_write(&params, sizeof(params), &residual, &ctrl->hmb.sg, attrs);

    if (unlikely(residual)) {
        trace_pci_nvme_err_invalid_dma();
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}
#endif /* 0 */

/* Find descriptor index */
static inline uint64_t lba_to_fa_desc_index(const NvmeFlashAddressMapParams *params, uint64_t lba)
{
    return lba / params->num_of_entries;
}

static inline uint64_t lba_to_fa_page_index(const NvmeFlashAddressMapParams *params, uint64_t lba)
{
    return lba % (params->set_size / params->entry_size);
}

#if 0
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
#endif
