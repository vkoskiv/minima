#include <fs/dev_block.h>
#include <vkern.h>
#include <assert.h>
#include <errno.h>

v_ilist block_devices = V_ILIST_INIT(block_devices);
static int s_blockdev_initialized = 0;
struct semaphore s_block_devices_lock;

static void dev_block_init(void) {
	sem_init(&s_block_devices_lock, 1, "block_devices");
	s_blockdev_initialized = 1;
}

int dev_block_get_block_count(struct dev_block *dev) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_count)
		return -ENOTSUP;
	sem_pend(&dev->blk_sem);
	int ret = dev->block_count((void *)dev);
	sem_post(&dev->blk_sem);
	return ret;
}

int dev_block_get_block_size(struct dev_block *dev) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_size)
		return -ENOTSUP;
	sem_pend(&dev->blk_sem);
	int ret = dev->block_size((void *)dev);
	sem_post(&dev->blk_sem);
	return ret;
}

int dev_block_read(struct dev_block *dev, unsigned int lba, unsigned char *out) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_read)
		return -ENOTSUP;
	sem_pend(&dev->blk_sem);
	int ret = dev->block_read((void *)dev, lba, out);
	sem_post(&dev->blk_sem);
	return ret;
}

int dev_block_write(struct dev_block *dev, unsigned int lba, const unsigned char *in) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_write)
		return -ENOTSUP;
	sem_pend(&dev->blk_sem);
	int ret = dev->block_write((void *)dev, lba, in);
	sem_post(&dev->blk_sem);
	return ret;
}

int dev_block_register(struct dev_block *dev) {
	if (!s_blockdev_initialized)
		dev_block_init();
	sem_pend(&s_block_devices_lock);
	v_ilist_prepend(&dev->base.linkage, &block_devices);
	sem_post(&s_block_devices_lock);
	return 0;
}

int dev_block_unregister(struct dev_block *dev) {
	if (!s_blockdev_initialized)
		dev_block_init();
	sem_pend(&s_block_devices_lock);
	v_ilist_remove(&dev->base.linkage);
	sem_post(&s_block_devices_lock);
	return 0;
}

struct dev_block *dev_block_open(const char *name) {
	if (!s_blockdev_initialized)
		dev_block_init();
	struct dev_block *device = NULL;
	sem_pend(&s_block_devices_lock);
	{
		v_ilist *pos;
		v_ilist_for_each(pos, &block_devices) {
			struct device *d = v_ilist_get(pos, struct device, linkage);
			if (strcmp(d->name, name) == 0) {
				if (!d->refs) {
					struct dev_block *blk = (struct dev_block *)d;
					sem_init(&blk->blk_sem, 1, d->name);
				}
				++d->refs;
				device = (struct dev_block *)d;
				break;
			}
		}
	}
	sem_post(&s_block_devices_lock);
	return device;
}

void dev_block_close(struct dev_block *dev) {
	if (!s_blockdev_initialized)
		dev_block_init();
	assert(dev);
	--dev->base.refs;
	// TODO: sem_pend & teardown
}
