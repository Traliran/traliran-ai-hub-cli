/*
 * storage.h - persistent key/value store backed by a JSON file.
 * Mirrors the web app's STORAGE (localStorage/IndexedDB) facade.
 */
#ifndef HUB_STORAGE_H
#define HUB_STORAGE_H

void         storage_init(void);
void         storage_save(void);               /* flush to disk */
const char  *storage_get(const char *key);     /* never NULL, may be "" */
void         storage_set(const char *key, const char *value);
void         storage_remove(const char *key);

#endif /* HUB_STORAGE_H */