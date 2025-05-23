/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Phobos backend adapters
 */
#ifndef _PHO_IO_H
#define _PHO_IO_H

#include "pho_types.h"
#include "pho_attrs.h"
#include <errno.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#define PHO_EA_OBJECT_UUID_NAME     "object_uuid"
#define PHO_EA_OBJECT_SIZE_NAME     "object_size"
#define PHO_EA_VERSION_NAME         "version"
#define PHO_EA_UMD_NAME             "user_md"
#define PHO_EA_MD5_NAME             "md5"
#define PHO_EA_XXH128_NAME          "xxh128"
#define PHO_EA_LAYOUT_NAME          "layout"
#define PHO_EA_EXTENT_OFFSET_NAME   "extent_offset"
#define PHO_EA_COPY_NAME            "copy_name"

#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)

/* FIXME: only 2 combinations are used: REPLACE | NO_REUSE and DELETE */
enum pho_io_flags {
    PHO_IO_MD_ONLY    = (1 << 0),   /**< Only operate on object MD */
    PHO_IO_REPLACE    = (1 << 1),   /**< Replace the entry if it exists */
    PHO_IO_SYNC_FILE  = (1 << 2),   /**< Sync file data to media on close */
    PHO_IO_NO_REUSE   = (1 << 3),   /**< Drop file contents from system cache */
    PHO_IO_DELETE     = (1 << 4),   /**< Delete extent from media */
};

/**
 * Describe an I/O operation. This can be either or both data and metadata
 * request.
 * For MD GET request, the metadata buffer is expected to contain the requested
 * keys. The associated values will be ignored and replaced by the retrieved
 * ones.
 */
struct pho_io_descr {
    /** Reference to the I/O adapter for this I/O decriptor */
    struct io_adapter_module *iod_ioa;
    enum pho_io_flags    iod_flags;    /**< PHO_IO_* flags */
    int                  iod_fd;       /**< Local FD */
    size_t               iod_size;     /**< Operation size */
    struct pho_ext_loc  *iod_loc;      /**< Extent location */
    struct pho_attrs     iod_attrs;    /**< In/Out metadata operations buffer */
    void                *iod_ctx;      /**< IO adapter private context */
    int                  iod_rc;       /**< Return code of IO operation */
};

struct object_metadata {
    struct pho_attrs object_attrs;
    ssize_t object_size;
    int object_version;
    const char *layout_name;
    const char *object_uuid;
    const char *copy_name;
};

/**
 * An I/O adapter (IOA) is a vector of functions that provide access to a media.
 * They should be invoked via their corresponding wrappers below. Refer to
 * them for more precise explanation about each call.
 *
 * Some calls are strictly mandatory, and assertion failures may happen if they
 * are found to be missing. Others aren't for which the wrappers will return
 * -ENOTSUP if they are not implemented in an IOA.
 *
 * Refer to the documentation of wrappers for what exactly is expected, done and
 * returned by the functions.
 */
struct pho_io_adapter_module_ops {
    int (*ioa_get)(const char *extent_desc, struct pho_io_descr *iod);
    int (*ioa_del)(struct pho_io_descr *iod);
    int (*iod_from_fd)(struct pho_io_descr *iod, int fd);
    int (*ioa_open)(const char *extent_desc, struct pho_io_descr *iod,
                    bool is_put);
    int (*ioa_write)(struct pho_io_descr *iod, const void *buf, size_t count);
    ssize_t (*ioa_read)(struct pho_io_descr *iod, void *buf, size_t count);
    int (*ioa_close)(struct pho_io_descr *iod);
    int (*ioa_medium_sync)(const char *root_path, json_t **message);
    ssize_t (*ioa_preferred_io_size)(struct pho_io_descr *iod);
    int (*ioa_set_md)(const char *extent_desc, struct pho_io_descr *iod);
    int (*ioa_get_common_xattrs_from_extent)(struct pho_io_descr *iod,
                                             struct layout_info *lyt_info,
                                             struct extent *extent_to_insert,
                                             struct object_info *obj_info);
    ssize_t (*ioa_size)(struct pho_io_descr *iod);
};

struct io_adapter_module {
    struct module_desc desc;       /**< Description of this io_adapter_module */
    const struct pho_io_adapter_module_ops *ops;
                                   /**< Operations of this io_adapter_module */
};

/**
 * Retrieve IO functions for the given filesystem type.
 */
int get_io_adapter(enum fs_type fstype, struct io_adapter_module **ioa);

/**
 * Get an extent from a media.
 * All I/O adapters must implement this call.
 *
 * The I/O descriptor must be filled as follow:
 * iod_fd       Destination data stream file descriptor
 *              (must be positionned at desired write offset)
 * iod_size     Amount of data to be read.
 * iod_loc      Extent location identifier. The call may set
 *              loc->extent.address if it is missing
 * iod_attrs    List of metadata blob names to be retrieved. The function fills
 *              the value fields with the retrieved values.
 * iod_flags    Bitmask of PHO_IO_* flags
 *
 *
 * \param[in]       ioa         Suitable I/O adapter for the media
 * \param[in]       extent_desc Null-terminated extent description
 * \param[in,out]   iod         I/O descriptor (see above)
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_get(const struct io_adapter_module *ioa,
                          const char *extent_desc,
                          struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_get != NULL);
    return ioa->ops->ioa_get(extent_desc, iod);
}

/**
 * Remove extent from the backend.
 * All I/O adapters must implement this call.
 *
 * \param[in]  ioa  Suitable I/O adapter for the media.
 * \param[in]  loc  Location of the extent to remove. The call may set
 *                  loc->extent.address if it is missing.
 *
 * \return 0 on success, negative error code on failure.
 */
static inline int ioa_del(const struct io_adapter_module *ioa,
                          struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_del != NULL);
    return ioa->ops->ioa_del(iod);
}

/**
 * Flush all pending IOs to stable storage.
 * I/O adapters may implement this call.
 *
 * \param[in]   ioa         Suitable I/O adapter for the media
 * \param[in]   root_path   Root path of the mounted medium to flush
 * \param[out]  message     Json message allocated in case of error, NULL
 *                          otherwise. Must be freed by the caller
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_medium_sync(const struct io_adapter_module *ioa,
                                  const char *root_path, json_t **message)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    if (ioa->ops->ioa_medium_sync == NULL)
        return -ENOTSUP;

    return ioa->ops->ioa_medium_sync(root_path, message);
}

/**
 * Retrieves the preferred IO size for the given IO descriptor.
 *
 * \param[in]       ioa         Suitable I/O adapter for the media
 * \param[in,out]   iod         I/O descriptor (see above)
 *
 * \return a positive size on success, negative error code on failure
 * \retval -ENOTSUP the I/O adapter does not provide this function
 */
static inline ssize_t ioa_preferred_io_size(const struct io_adapter_module *ioa,
                                            struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    if (ioa->ops->ioa_preferred_io_size == NULL)
        return -ENOTSUP;

    return ioa->ops->ioa_preferred_io_size(iod);
}

/**
 * Open all needed ressources on a media to do a transfer.
 * All I/O adapters must implement this call.
 * The I/O descriptor iod->iod_ctx is allocated and filled by ioa_open().
 * iod->iod_ctx must be closed and freed by calling ioa_close.
 *
 * The I/O descriptor must be filled as follows:
 * iod_loc      Extent location identifier. If it is missing by the caller, the
 *              call will set loc->extent.address to indicate where the extent
 *              has been located.
 * iod_attrs    PUT: list of metadata blob k/v to set for the extent. If a value
 *              is NULL, previous value of the attribute is removed.
 *              GET: List of metadata blob names to be retrieved. The function
 *              fills the value fields with the retrieved values.
 * iod_flags    Bitmask of PHO_IO_* flags.
 *
 *
 * \param[in]       ioa         Suitable I/O adapter for the media
 * \param[in]       extent_desc Null-terminated extent description
 * \param[in,out]   iod         I/O descriptor (see above)
 * \param[in]       is_put      Must be true when opening for a put/write and
 *                              false when opening for a get/read
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_open(const struct io_adapter_module *ioa,
                           const char *extent_desc, struct pho_io_descr *iod,
                           bool is_put)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_open != NULL);
    return ioa->ops->ioa_open(extent_desc, iod, is_put);
}

static inline int iod_from_fd(const struct io_adapter_module *ioa,
                              struct pho_io_descr *iod, int fd)
{
    assert(ioa);
    assert(ioa->ops);
    if (!ioa->ops->iod_from_fd)
        return -ENOTSUP;

    return ioa->ops->iod_from_fd(iod, fd);
}

/**
 * Write data provided by the input buffer by appending into the IO adapter
 * private context.
 * All I/O adapters must implement this call.
 *
 * The I/O descriptor must be filled as follows:
 * iod_ctx      IO adapter private context
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in]       buf     Data to write
 * \param[in]       count   Size in byte of data to write from buf
 * \param[in,out]   iod     I/O descriptor (see above)
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_write(const struct io_adapter_module *ioa,
                            struct pho_io_descr *iod, const void *buf,
                            size_t count)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_write != NULL);
    return ioa->ops->ioa_write(iod, buf, count);
}

/**
 * Read data from the IO adapter private context to the output buffer.
 * All I/O adapters must implement this call for the extent copy/migration.
 *
 * \param[in]      ioa    Suitable I/O adapter for the media
 * \param[in]      iod    I/O descriptor
 * \param[in,out]  buf    Read data
 * \param[in]      count  Size in byte of buf
 * \return null or positive size on success, negative error code on failure;
 *         if not equal to count, it means there is no more data to read
 */
static inline ssize_t ioa_read(const struct io_adapter_module *ioa,
                               struct pho_io_descr *iod, void *buf,
                               size_t count)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_read != NULL);
    return ioa->ops->ioa_read(iod, buf, count);
}

/**
 * Clean and free the iod_ctx
 * All I/O adapters must implement this call.
 *
 * \param[in]       ioa         Suitable I/O adapter for the media
 * \param[in,out]   iod         I/O descriptor containing the iod_ctx to close.
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_close(const struct io_adapter_module *ioa,
                            struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_close != NULL);
    return ioa->ops->ioa_close(iod);
}

/**
 * If the \p iod's file descriptor is valid, set the metadata using it.
 * Otherwise, or if \p iod->ctx is null, set the \p iod's flag to
 * PHO_IO_MD_ONLY and call ioa_open.
 *
 * \param[in]       ioa         Suitable I/O adapter for the media.
 * \param[in]       extent_desc Null-terminated extent description.
 * \param[in,out]   iod         I/O descriptor (see above).
 *
 * \return 0 on success, negative error code on failure.
 */
static inline int ioa_set_md(const struct io_adapter_module *ioa,
                             const char *extent_desc, struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_set_md != NULL);
    return ioa->ops->ioa_set_md(extent_desc, iod);
}

/**
 * This function takes a file name, and parses it several times to retrieve
 * data contained in an extent's file name.
 *
 * @param[in]   ioa                 Suitable I/O adapter for the media.
 * @param[in]   iod                 I/O descriptor of the extent, must have
 *                                  at least the location set
 * @param[out]  lyt_info            Contains information about the layout of
 *                                  the extent to add.
 * @param[out]  extent_to_insert    Contains information about the extent to
 *                                  add.
 * @param[out]  obj_info            Contains information about the object
 *                                  related to the extent to add.
 *
 * @return      0 on success,
 *              -EINVAL on failure.
 */
static inline int
ioa_get_common_xattrs_from_extent(const struct io_adapter_module *ioa,
                                  struct pho_io_descr *iod,
                                  struct layout_info *lyt_info,
                                  struct extent *extent_to_insert,
                                  struct object_info *obj_info)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    assert(ioa->ops->ioa_get_common_xattrs_from_extent != NULL);
    return ioa->ops->ioa_get_common_xattrs_from_extent(iod, lyt_info,
                                                       extent_to_insert,
                                                       obj_info);
}

/**
 * Retrieves the size of the extent.
 *
 * \param[in]       ioa         Suitable I/O adapter for the media
 * \param[in,out]   iod         I/O descriptor (see above)
 *
 * \return a positive size on success, negative error code on failure
 * \retval -ENOTSUP the I/O adapter does not provide this function
 */
static inline ssize_t ioa_size(const struct io_adapter_module *ioa,
                               struct pho_io_descr *iod)
{
    assert(ioa != NULL);
    assert(ioa->ops != NULL);
    if (ioa->ops->ioa_size == NULL)
        return -ENOTSUP;

    return ioa->ops->ioa_size(iod);
}

/**
 * Retrieve io_block_size value from config file
 *
 * A negative value is not a valid write block size, -EINVAL will be returned.
 * 0 is not a valid write block size, but can be used to indicate we want to
 * get the size from the backend storage, so no error will be returned.
 *
 * \param[out]      size        io_block_size value.
 * \param[in]       family      io_block_size family.
 *
 * \return 0 on success, negative error code on failure.
 */
int get_cfg_io_block_size(size_t *size, enum rsc_family family);

/**
 * Retrieve fs_block_size value from config file
 *
 * A negative value is not a valid fs block size, -EINVAL will be returned.
 *
 * \param[in]       family      Family of the filesystem.
 * \param[out]      size        fs_block_size value.
 *
 * \return 0 on success, negative error code on failure.
 */
int get_cfg_fs_block_size(enum rsc_family family, size_t *size);

/**
 * Retrieve the preferred IO size from the backend storage
 * if it was not set in the global "io" configuration.
 *
 * @param[out]    io_size   The IO size to be used.
 * @param[in]     family    Family of the backend storage.
 * @param[in]     ioa       IO adapter to access the current storage backend.
 * @param[in,out] iod       IO decriptor to access the current object.
 */
void get_preferred_io_block_size(size_t *io_size, enum rsc_family family,
                                 const struct io_adapter_module *ioa,
                                 struct pho_io_descr *iod);

/**
 * If NULL as input, io_size is updated with the preferred IO size from iod.
 *
 * @param[in]       iod     IO decriptor to access the data.
 * @param[in, out]  io_size The IO size to be used.
 */
void update_io_size(struct pho_io_descr *iod, size_t *io_size);

/*
 * Copy an extent from a medium to another.
 *
 * The source I/O descriptor must be filled as follows:
 * iod_loc          Address and root path of the extent.
 * iod_size         Size of the extent.
 *
 * The target I/O descriptor must be filled as follows:
 * iod_loc          Root path of the extent.
 *
 * \param[in]       ioa_source  I/O adapter of the source medium.
 * \param[in, out]  iod_source  I/O descriptor of the source object.
 * \param[in]       ioa_target  I/O adapter of the target medium.
 * \param[in, out]  iod_target  I/O descriptor of the target object.
 * \param[in]       family      Family of the source medium.
 *
 * \return 0 on success, negative error code on failure.
 */
int copy_extent(struct io_adapter_module *ioa_source,
                struct pho_io_descr *iod_source,
                struct io_adapter_module *ioa_target,
                struct pho_io_descr *iod_target,
                enum rsc_family family);

/**
 * Set the common information regarding an object and the extent being
 * processed to the io adapter.
 *
 * \param[in]   ioa                 Suitable I/O adapter for the medium.
 * \param[in]   iod                 I/O descriptor containg the extent's
 *                                  general information
 * \param[in]   object_md           General object metadata
 *
 * @return      0 on success,
 *              -EINVAL on failure.
 */
int set_object_md(const struct io_adapter_module *ioa, struct pho_io_descr *iod,
                  struct object_metadata *object_md);

#endif
