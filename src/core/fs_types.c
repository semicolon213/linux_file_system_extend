#include "core/fs_types.h"

void fs_entry_free(FsEntry *entry) {
    if (entry == NULL) {
        return;
    }
    g_free(entry->name);
    g_free(entry);
}

FsEntry *fs_entry_copy(const FsEntry *entry) {
    FsEntry *copy;

    if (entry == NULL) {
        return NULL;
    }
    copy = g_memdup2(entry, sizeof(*entry));
    copy->name = g_strdup(entry->name);
    return copy;
}
