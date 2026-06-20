#include "apps/trmnl/trmnl_cache.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

namespace paper_screen {

namespace {

constexpr const char* kLastImagePath = "last-image.png";
constexpr const char* kLastMetadataPath = "last-metadata.json";
constexpr const char* kCacheIndexPath = "cache-index.jsonl";
constexpr size_t kMaxCachedImageBytes = 1024 * 1024;
constexpr size_t kMaxCacheEntries = 6;
constexpr size_t kCacheIndexCapacity = 4096;
constexpr uint32_t kMinimumCacheTtlSeconds = 300;

struct CacheEntry {
    char leaf[40] = {};
    int response_status = 0;
    uint32_t image_bytes = 0;
    uint32_t image_url_hash = 0;
    uint32_t image_visual_hash = 0;
    uint32_t refresh_seconds = 1800;
    char image_name[128] = {};
    char metadata_etag[96] = {};
    char image_etag[96] = {};
    int64_t saved_epoch = 0;
    int64_t expires_epoch = 0;
};

uint32_t parse_uint_field(const char* text, const char* key, uint32_t fallback)
{
    if (text == nullptr || key == nullptr) {
        return fallback;
    }

    const char* cursor = std::strstr(text, key);
    if (cursor == nullptr) {
        return fallback;
    }

    cursor += std::strlen(key);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ':' || *cursor == '"') {
        ++cursor;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(cursor, &end, 10);
    return end != cursor ? static_cast<uint32_t>(parsed) : fallback;
}

int64_t parse_int64_field(const char* text, const char* key, int64_t fallback)
{
    if (text == nullptr || key == nullptr) {
        return fallback;
    }

    const char* cursor = std::strstr(text, key);
    if (cursor == nullptr) {
        return fallback;
    }

    cursor += std::strlen(key);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ':' || *cursor == '"') {
        ++cursor;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(cursor, &end, 10);
    return end != cursor ? static_cast<int64_t>(parsed) : fallback;
}

void parse_string_field(const char* text, const char* key, char* out, size_t out_size)
{
    if (text == nullptr || key == nullptr || out == nullptr || out_size == 0) {
        return;
    }

    const char* cursor = std::strstr(text, key);
    if (cursor == nullptr) {
        return;
    }

    cursor += std::strlen(key);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ':') {
        ++cursor;
    }
    if (*cursor == '"') {
        ++cursor;
    }

    size_t i = 0;
    while (cursor[i] != '\0' && cursor[i] != '"' && i + 1 < out_size) {
        out[i] = cursor[i];
        ++i;
    }
    out[i] = '\0';
}

uint8_t* allocate_image(size_t size)
{
#ifdef ARDUINO
    uint8_t* image = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (image != nullptr) {
        return image;
    }
    return static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
#else
    return static_cast<uint8_t*>(std::malloc(size));
#endif
}

char* allocate_text_buffer(size_t size)
{
#ifdef ARDUINO
    char* buffer = static_cast<char*>(heap_caps_calloc(size, sizeof(char), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer != nullptr) {
        return buffer;
    }
#endif
    return static_cast<char*>(std::calloc(size, sizeof(char)));
}

CacheEntry* allocate_entries(size_t count)
{
#ifdef ARDUINO
    CacheEntry* entries = static_cast<CacheEntry*>(heap_caps_calloc(count, sizeof(CacheEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (entries != nullptr) {
        return entries;
    }
#endif
    return static_cast<CacheEntry*>(std::calloc(count, sizeof(CacheEntry)));
}

void free_image(uint8_t* image)
{
    if (image == nullptr) {
        return;
    }
#ifdef ARDUINO
    heap_caps_free(image);
#else
    std::free(image);
#endif
}

void free_text_buffer(char* buffer)
{
    if (buffer == nullptr) {
        return;
    }
#ifdef ARDUINO
    heap_caps_free(buffer);
#else
    std::free(buffer);
#endif
}

void free_entries(CacheEntry* entries)
{
    if (entries == nullptr) {
        return;
    }
#ifdef ARDUINO
    heap_caps_free(entries);
#else
    std::free(entries);
#endif
}

uint32_t hash_text(const char* text)
{
    uint32_t hash = 2166136261UL;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text == nullptr ? "" : text);
    while (*cursor != '\0') {
        hash ^= *cursor++;
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

void image_leaf_for(const TrmnlSnapshot& snapshot, char* out, size_t out_size)
{
    uint32_t identity = snapshot.image_url_hash;
    if (identity == 0 && snapshot.response.image_etag[0] != '\0') {
        identity = hash_text(snapshot.response.image_etag);
    }
    if (identity == 0 && snapshot.response.image_name[0] != '\0') {
        identity = hash_text(snapshot.response.image_name);
    }
    if (identity == 0) {
        identity = snapshot.image_visual_hash;
    }
    std::snprintf(out, out_size, "image-%08lx.png", static_cast<unsigned long>(identity));
}

uint32_t cache_ttl_seconds(const TrmnlSnapshot& snapshot)
{
    return snapshot.response.refresh_seconds >= kMinimumCacheTtlSeconds
        ? snapshot.response.refresh_seconds
        : kMinimumCacheTtlSeconds;
}

bool entry_expired(const CacheEntry& entry, int64_t now_epoch)
{
    return now_epoch > 0 && entry.expires_epoch > 0 && now_epoch > entry.expires_epoch;
}

TrmnlSnapshot snapshot_from_entry(const CacheEntry& entry)
{
    TrmnlSnapshot snapshot;
    snapshot.status = TrmnlFetchStatus::Ready;
    snapshot.response.status = entry.response_status;
    snapshot.image_bytes = entry.image_bytes;
    snapshot.image_url_hash = entry.image_url_hash;
    snapshot.image_visual_hash = entry.image_visual_hash;
    snapshot.image_downloaded = false;
    snapshot.response.refresh_seconds = entry.refresh_seconds;
    std::snprintf(snapshot.response.image_name,
                  sizeof(snapshot.response.image_name),
                  "%s",
                  entry.image_name);
    std::snprintf(snapshot.response.metadata_etag,
                  sizeof(snapshot.response.metadata_etag),
                  "%s",
                  entry.metadata_etag);
    std::snprintf(snapshot.response.image_etag,
                  sizeof(snapshot.response.image_etag),
                  "%s",
                  entry.image_etag);
    return snapshot;
}

void fill_entry_from_snapshot(CacheEntry& entry,
                              const char* leaf,
                              const TrmnlSnapshot& snapshot,
                              size_t image_size,
                              int64_t saved_epoch)
{
    std::snprintf(entry.leaf, sizeof(entry.leaf), "%s", leaf == nullptr ? "" : leaf);
    entry.response_status = snapshot.response.status;
    entry.image_bytes = static_cast<uint32_t>(image_size);
    entry.image_url_hash = snapshot.image_url_hash;
    entry.image_visual_hash = snapshot.image_visual_hash;
    entry.refresh_seconds = cache_ttl_seconds(snapshot);
    std::snprintf(entry.image_name,
                  sizeof(entry.image_name),
                  "%s",
                  snapshot.response.image_name);
    std::snprintf(entry.metadata_etag,
                  sizeof(entry.metadata_etag),
                  "%s",
                  snapshot.response.metadata_etag);
    std::snprintf(entry.image_etag,
                  sizeof(entry.image_etag),
                  "%s",
                  snapshot.response.image_etag);
    entry.saved_epoch = saved_epoch;
    entry.expires_epoch = saved_epoch > 0 ? saved_epoch + static_cast<int64_t>(entry.refresh_seconds) : 0;
}

bool parse_entry(const char* line, CacheEntry* entry)
{
    if (line == nullptr || entry == nullptr) {
        return false;
    }

    CacheEntry parsed;
    parse_string_field(line, "\"leaf\"", parsed.leaf, sizeof(parsed.leaf));
    if (parsed.leaf[0] == '\0') {
        return false;
    }

    parsed.response_status = static_cast<int>(parse_uint_field(line, "\"status\"", 0));
    parsed.image_bytes = parse_uint_field(line, "\"image_bytes\"", 0);
    parsed.image_url_hash = parse_uint_field(line, "\"image_url_hash\"", 0);
    parsed.image_visual_hash = parse_uint_field(line, "\"image_visual_hash\"", 0);
    parsed.refresh_seconds = parse_uint_field(line, "\"refresh_seconds\"", 1800);
    parsed.saved_epoch = parse_int64_field(line, "\"saved_epoch\"", 0);
    parsed.expires_epoch = parse_int64_field(line, "\"expires_epoch\"", 0);
    parse_string_field(line, "\"image_name\"", parsed.image_name, sizeof(parsed.image_name));
    parse_string_field(line, "\"metadata_etag\"", parsed.metadata_etag, sizeof(parsed.metadata_etag));
    parse_string_field(line, "\"image_etag\"", parsed.image_etag, sizeof(parsed.image_etag));

    *entry = parsed;
    return true;
}

size_t load_entries(AppContext& context, CacheEntry* entries, size_t capacity)
{
    if (entries == nullptr || capacity == 0 || !context.storage.exists(kCacheIndexPath)) {
        return 0;
    }

    char* index = allocate_text_buffer(kCacheIndexCapacity);
    if (index == nullptr) {
        context.log.warn("cache index allocation failed");
        return 0;
    }

    size_t index_size = 0;
    if (!context.storage.read_file(kCacheIndexPath,
                                   reinterpret_cast<uint8_t*>(index),
                                   kCacheIndexCapacity - 1,
                                   &index_size)) {
        free_text_buffer(index);
        return 0;
    }
    index[index_size] = '\0';

    size_t count = 0;
    char* line = index;
    while (line != nullptr && *line != '\0' && count < capacity) {
        char* next = std::strchr(line, '\n');
        if (next != nullptr) {
            *next = '\0';
            ++next;
        }
        CacheEntry entry;
        if (parse_entry(line, &entry)) {
            entries[count++] = entry;
        }
        line = next;
    }
    free_text_buffer(index);
    return count;
}

bool write_entries(AppContext& context, const CacheEntry* entries, size_t count)
{
    char* index = allocate_text_buffer(kCacheIndexCapacity);
    if (index == nullptr) {
        context.log.warn("cache index allocation failed");
        return false;
    }

    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        const CacheEntry& entry = entries[i];
        const int written = std::snprintf(index + offset,
                                          kCacheIndexCapacity - offset,
                                          "{\"leaf\":\"%s\",\"status\":%d,\"image_bytes\":%lu,"
                                          "\"image_url_hash\":%lu,\"image_visual_hash\":%lu,"
                                          "\"refresh_seconds\":%lu,\"saved_epoch\":%lld,"
                                          "\"expires_epoch\":%lld,\"image_name\":\"%s\","
                                          "\"metadata_etag\":\"%s\",\"image_etag\":\"%s\"}\n",
                                          entry.leaf,
                                          entry.response_status,
                                          static_cast<unsigned long>(entry.image_bytes),
                                          static_cast<unsigned long>(entry.image_url_hash),
                                          static_cast<unsigned long>(entry.image_visual_hash),
                                          static_cast<unsigned long>(entry.refresh_seconds),
                                          static_cast<long long>(entry.saved_epoch),
                                          static_cast<long long>(entry.expires_epoch),
                                          entry.image_name,
                                          entry.metadata_etag,
                                          entry.image_etag);
        if (written <= 0 || static_cast<size_t>(written) >= kCacheIndexCapacity - offset) {
            free_text_buffer(index);
            context.log.warn("cache index too large");
            return false;
        }
        offset += static_cast<size_t>(written);
    }

    const bool ok = context.storage.write_text_atomic(kCacheIndexPath, index);
    free_text_buffer(index);
    return ok;
}

bool entry_matches_probe(const CacheEntry& entry, const TrmnlSnapshot& probe)
{
    return (probe.image_url_hash != 0 && entry.image_url_hash == probe.image_url_hash)
        || (probe.image_visual_hash != 0 && entry.image_visual_hash == probe.image_visual_hash)
        || (probe.response.image_etag[0] != '\0'
            && std::strcmp(entry.image_etag, probe.response.image_etag) == 0);
}

bool load_entry_image(AppContext& context,
                      const CacheEntry& entry,
                      TrmnlSnapshot* snapshot,
                      uint8_t** image_data,
                      size_t* image_size)
{
    size_t cached_size = 0;
    if (!context.storage.file_size(entry.leaf, &cached_size)
        || cached_size == 0
        || cached_size > kMaxCachedImageBytes) {
        context.log.warn("cached image size invalid");
        return false;
    }

    uint8_t* cached_image = allocate_image(cached_size);
    if (cached_image == nullptr) {
        context.log.warn("cached image allocation failed");
        return false;
    }

    size_t bytes_read = 0;
    if (!context.storage.read_file(entry.leaf, cached_image, cached_size, &bytes_read)
        || bytes_read != cached_size) {
        free_image(cached_image);
        context.log.warn("cached image read failed");
        return false;
    }

    TrmnlSnapshot cached_snapshot = snapshot_from_entry(entry);
    cached_snapshot.image_bytes = static_cast<uint32_t>(cached_size);

    *snapshot = cached_snapshot;
    *image_data = cached_image;
    *image_size = cached_size;
    return true;
}

}  // namespace

bool TrmnlCache::save_last_good(AppContext& context,
                                const TrmnlSnapshot& snapshot,
                                const uint8_t* image_data,
                                size_t image_size) const
{
    if (snapshot.status != TrmnlFetchStatus::Ready || image_data == nullptr || image_size == 0) {
        return false;
    }

    if (!context.storage.ensure_directory()) {
        context.log.warn("cache directory unavailable");
        return false;
    }

    cleanup(context);

    char image_leaf[40] = {};
    image_leaf_for(snapshot, image_leaf, sizeof(image_leaf));
    if (!context.storage.write_file_atomic(image_leaf, image_data, image_size)) {
        context.log.warn("cache image write failed");
        return false;
    }

    CacheEntry* existing = allocate_entries(kMaxCacheEntries + 2);
    CacheEntry* updated = allocate_entries(kMaxCacheEntries);
    if (existing == nullptr || updated == nullptr) {
        free_entries(existing);
        free_entries(updated);
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t existing_count = load_entries(context, existing, kMaxCacheEntries + 2);
    CacheEntry fresh;
    fill_entry_from_snapshot(fresh, image_leaf, snapshot, image_size, context.clock.now_epoch());
    updated[0] = fresh;

    size_t updated_count = 1;
    for (size_t i = 0; i < existing_count && updated_count < kMaxCacheEntries; ++i) {
        if (std::strcmp(existing[i].leaf, image_leaf) == 0 || entry_expired(existing[i], context.clock.now_epoch())) {
            continue;
        }
        updated[updated_count++] = existing[i];
    }

    for (size_t i = 0; i < existing_count; ++i) {
        bool retained = false;
        for (size_t j = 0; j < updated_count; ++j) {
            if (std::strcmp(existing[i].leaf, updated[j].leaf) == 0) {
                retained = true;
                break;
            }
        }
        if (!retained) {
            context.storage.remove_file(existing[i].leaf);
        }
    }

    if (!write_entries(context, updated, updated_count)) {
        free_entries(existing);
        free_entries(updated);
        context.log.warn("cache metadata write failed");
        return false;
    }

    free_entries(existing);
    free_entries(updated);
    context.log.info("cached last good image");
    return true;
}

bool TrmnlCache::load_metadata(AppContext& context, TrmnlSnapshot* snapshot) const
{
    if (snapshot == nullptr) {
        return false;
    }

    CacheEntry* entries = allocate_entries(kMaxCacheEntries);
    if (entries == nullptr) {
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries);
    for (size_t i = 0; i < count; ++i) {
        if (!entry_expired(entries[i], context.clock.now_epoch()) && context.storage.exists(entries[i].leaf)) {
            *snapshot = snapshot_from_entry(entries[i]);
            free_entries(entries);
            return true;
        }
    }

    free_entries(entries);
    return false;
}

size_t TrmnlCache::load_recent_metadata(AppContext& context, TrmnlSnapshot* snapshots, size_t capacity) const
{
    if (snapshots == nullptr || capacity == 0) {
        return 0;
    }

    CacheEntry* entries = allocate_entries(kMaxCacheEntries);
    if (entries == nullptr) {
        context.log.warn("cache entry allocation failed");
        return 0;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries);
    size_t written = 0;
    for (size_t i = 0; i < count && written < capacity; ++i) {
        if (!entry_expired(entries[i], context.clock.now_epoch()) && context.storage.exists(entries[i].leaf)) {
            snapshots[written++] = snapshot_from_entry(entries[i]);
        }
    }
    free_entries(entries);
    return written;
}

bool TrmnlCache::load_matching(AppContext& context,
                               const TrmnlSnapshot& probe,
                               TrmnlSnapshot* snapshot,
                               uint8_t** image_data,
                               size_t* image_size) const
{
    if (snapshot == nullptr || image_data == nullptr || image_size == nullptr) {
        return false;
    }
    *image_data = nullptr;
    *image_size = 0;

    CacheEntry* entries = allocate_entries(kMaxCacheEntries);
    if (entries == nullptr) {
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries);
    for (size_t i = 0; i < count; ++i) {
        if (entry_expired(entries[i], context.clock.now_epoch())
            || !context.storage.exists(entries[i].leaf)
            || !entry_matches_probe(entries[i], probe)) {
            continue;
        }
        if (load_entry_image(context, entries[i], snapshot, image_data, image_size)) {
            free_entries(entries);
            context.log.info("loaded matching cached image");
            return true;
        }
        free_entries(entries);
        return false;
    }

    free_entries(entries);
    return false;
}

bool TrmnlCache::load_last_good(AppContext& context,
                                TrmnlSnapshot* snapshot,
                                uint8_t** image_data,
                                size_t* image_size) const
{
    if (snapshot == nullptr || image_data == nullptr || image_size == nullptr) {
        return false;
    }
    *image_data = nullptr;
    *image_size = 0;

    CacheEntry* entries = allocate_entries(kMaxCacheEntries);
    if (entries == nullptr) {
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries);
    const CacheEntry* selected = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (!entry_expired(entries[i], context.clock.now_epoch()) && context.storage.exists(entries[i].leaf)) {
            selected = &entries[i];
            break;
        }
    }

    if (selected == nullptr) {
        free_entries(entries);
        context.log.warn("no cached image available");
        return false;
    }

    if (!load_entry_image(context, *selected, snapshot, image_data, image_size)) {
        free_entries(entries);
        return false;
    }
    free_entries(entries);
    context.log.info("loaded cached last good image");
    return true;
}

bool TrmnlCache::clear(AppContext& context) const
{
    CacheEntry* entries = allocate_entries(kMaxCacheEntries + 2);
    if (entries == nullptr) {
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries + 2);
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        ok = context.storage.remove_file(entries[i].leaf) && ok;
    }
    ok = context.storage.remove_file(kCacheIndexPath) && ok;
    ok = context.storage.remove_file(kLastImagePath) && ok;
    ok = context.storage.remove_file(kLastMetadataPath) && ok;
    free_entries(entries);
    if (ok) {
        context.log.info("cleared cache");
    } else {
        context.log.warn("cache clear incomplete");
    }
    return ok;
}

bool TrmnlCache::cleanup(AppContext& context) const
{
    CacheEntry* entries = allocate_entries(kMaxCacheEntries + 2);
    CacheEntry* retained = allocate_entries(kMaxCacheEntries);
    if (entries == nullptr || retained == nullptr) {
        free_entries(entries);
        free_entries(retained);
        context.log.warn("cache entry allocation failed");
        return false;
    }

    const size_t count = load_entries(context, entries, kMaxCacheEntries + 2);
    if (count == 0) {
        context.storage.remove_file(kLastImagePath);
        context.storage.remove_file(kLastMetadataPath);
        free_entries(entries);
        free_entries(retained);
        return true;
    }

    size_t retained_count = 0;
    bool ok = true;
    const int64_t now = context.clock.now_epoch();
    for (size_t i = 0; i < count; ++i) {
        const bool keep = !entry_expired(entries[i], now)
            && context.storage.exists(entries[i].leaf)
            && retained_count < kMaxCacheEntries;
        if (keep) {
            retained[retained_count++] = entries[i];
        } else {
            ok = context.storage.remove_file(entries[i].leaf) && ok;
        }
    }

    context.storage.remove_file(kLastImagePath);
    context.storage.remove_file(kLastMetadataPath);
    ok = write_entries(context, retained, retained_count) && ok;
    free_entries(entries);
    free_entries(retained);
    if (ok) {
        context.log.info("cache cleanup complete");
    } else {
        context.log.warn("cache cleanup incomplete");
    }
    return ok;
}

void TrmnlCache::release_image(uint8_t* image_data) const
{
    free_image(image_data);
}

}  // namespace paper_screen
