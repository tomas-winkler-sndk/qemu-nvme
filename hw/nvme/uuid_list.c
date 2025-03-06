/*
 *  QEMU NVMe UUID LIST functions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2025 Sandisk Corporation or its affiliates
 *
 */

#include "qemu/osdep.h"
#include "qapi/visitor.h"
#include "qemu/ctype.h"
#include "qemu/uuid.h"
#include "qemu/log.h"
#include "nvme.h"

static void free_uuid_list( NvmeUUIDList *uuid_list)
{
    NvmeUUIDListEntry *entry, *_next;
    QLIST_FOREACH_SAFE(entry, uuid_list, next, _next) {
        QLIST_REMOVE(entry, next);
        g_free(entry);
    }
    QLIST_INIT(uuid_list);
}

static void release_uuid_list(Object *obj, const char *name, void *opaque)
{
    const Property *prop = opaque;
    free_uuid_list(object_field_prop_ptr(obj, prop));
}

#define NVME_UUID_LIST_ENTRY_STR_MAX (sizeof("256:") + UUID_STR_LEN)
#define NVME_UUID_LIST_BUFFER_MAX \
        ((NVME_UUID_LIST_ENTRY_STR_MAX + 1) * NVME_ID_UUID_LIST_MAX)

static int nvme_unparse_uuid_list_entry(NvmeUUIDListEntry *entry,
                                       char *buffer, size_t buf_size)
{
    int cnt;

    cnt = snprintf(buffer, buf_size, "%" SCNu8 ":",  entry->idassoc);
    if (cnt < 0) {
        return cnt;
    }
    if (buf_size - cnt <= UUID_STR_LEN) {
        return -1;
    }
    qemu_uuid_unparse(&entry->uuid, &buffer[cnt]);

    return cnt + UUID_STR_LEN - 1;
}

static void get_uuid_list(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    const Property *prop = opaque;
    NvmeUUIDList *uuid_list = object_field_prop_ptr(obj, prop);
    NvmeUUIDListEntry *entry;
    char buffer[NVME_UUID_LIST_BUFFER_MAX] = {0};
    size_t buf_size = sizeof(buffer);
    char *p = buffer;
    ssize_t cnt = 0;

    QLIST_FOREACH(entry, uuid_list, next) {
        if (buf_size <= cnt) {
            break;
        }
        int ret = nvme_unparse_uuid_list_entry(entry,
                                               &buffer[cnt], buf_size - cnt);
        if (ret < 0) {
            break;
        }
        cnt += ret;
        ret = snprintf(&buffer[cnt], buf_size - cnt, ";");
        if (ret < 0) {
            break;
        }
        cnt += ret;
    }
    /* drop the trailing ; */
    if (cnt > 0) {
        buffer[cnt - 1] = '\0';
    }

    visit_type_str(v, name, &p, errp);
}

static NvmeUUIDListEntry *nvme_parse_uuid_list_entry(char *param, Error **errp)
{
    char *str = param;
    char *idassoc_str;
    char *uuid_str;
    char *saveptr;

    uint8_t idassoc;
    QemuUUID uuid;
    NvmeUUIDListEntry *entry = NULL;

    idassoc_str = strtok_r(str, ":", &saveptr);
    if (idassoc_str == NULL) {
        error_setg(errp, "Invalid UUID list format");
        return NULL;
    }

    uuid_str = strtok_r(NULL, ":", &saveptr);
    if (uuid_str == NULL) {
        error_setg(errp, "Invalid UUID list format");
        return NULL;
    }

    if (sscanf(idassoc_str, "%" SCNu8, &idassoc) != 1) {
        error_setg(errp, "Invalid UUID list format: association id");
        return NULL;
    }

    if (idassoc & ~NVME_ID_UUID_HDR_ASSOCIATION_MASK) {
        error_setg(errp, "Invalid UUID list format: "
                   "association id should be in range [0-2]");
        return NULL;
    }

    if (qemu_uuid_parse(uuid_str, &uuid) < 0) {
        error_setg(errp, "Invalid UUID list format: malformatted uuid");
        return NULL;
    }

    entry = g_new0(NvmeUUIDListEntry, 1);
    entry->idassoc = idassoc;
    entry->uuid = uuid;

    return entry;
}

static void set_uuid_list(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    const Property *prop = opaque;
    NvmeUUIDList *uuid_list = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    char **entries = g_strsplit(str, ";", -1);
    int count = g_strv_length(entries);

    if (count >= NVME_ID_UUID_LIST_MAX) {
        error_setg(errp, "Too many UUID entries (max %d)",
                   NVME_ID_UUID_LIST_MAX);
        g_strfreev(entries);
        return;
    }

    QLIST_INIT(uuid_list);
    /* The entries must be in correct order */
    for (int i = count; i >= 0; i--) {
        /* Skip empty strings from g_strsplit (e.g., ";;") */
        if (!entries[i] || entries[i][0] == '\0') {
            continue;
        }
        NvmeUUIDListEntry *entry = nvme_parse_uuid_list_entry(entries[i], errp);
        if (entry) {
            QLIST_INSERT_HEAD(uuid_list, entry, next);
        } else {
            free_uuid_list(uuid_list);
            break;
        }
    }
    g_strfreev(entries);
    g_free(str);
}

const PropertyInfo qdev_prop_nvme_uuid_list = {
    .type  = "str",
    .description = "UUID List  <association:uuid>;<association:uuid>",
    .get   = get_uuid_list,
    .set   = set_uuid_list,
    .release = release_uuid_list,
};
