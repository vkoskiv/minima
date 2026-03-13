#include <fs/dev_char.h>
#include <errno.h>
#include <assert.h>

v_ilist char_devices = V_ILIST_INIT(char_devices);
int s_chardev_initialized = 0;
struct semaphore s_chardev_lock;

int read(struct dev_char *dev, char *buf, size_t n) {
	if (!dev->read)
		return -ENODEV;
	return dev->read(&dev->base, buf, n);
}

int write(struct dev_char *dev, char *buf, size_t n) {
	if (!dev->write)
		return -ENODEV;
	return dev->write(&dev->base, buf, n);
}

static void dev_char_init(void) {
	sem_init(&s_chardev_lock, 1);
	s_chardev_initialized = 1;
}

int dev_char_register(struct dev_char *dev) {
	if (!s_chardev_initialized)
		dev_char_init();
	sem_pend(&s_chardev_lock);
	{
		v_ilist_prepend(&dev->base.linkage, &char_devices);
	}
	sem_post(&s_chardev_lock);
	return 0;
}

int dev_char_unregister(struct dev_char *dev) {
	if (!s_chardev_initialized)
		dev_char_init();
	sem_pend(&s_chardev_lock);
	{
		v_ilist_remove(&dev->base.linkage);
		// FIXME: free/deinit
	}
	sem_post(&s_chardev_lock);
	return 0;
}

struct dev_char *dev_char_open(const char *name) {
	if (!s_chardev_initialized)
		dev_char_init();

	struct dev_char *device = NULL;
	sem_pend(&s_chardev_lock);
	{
		v_ilist *pos;
		v_ilist_for_each(pos, &char_devices) {
			struct device *d = v_ilist_get(pos, struct device, linkage);
			if (strcmp(d->name, name) == 0) {
				++d->refs;
				device = (struct dev_char *)d;
				break;
			}
		}
	}
	sem_post(&s_chardev_lock);
	return device;
}

void dev_char_close(struct dev_char *dev) {
	if (!s_chardev_initialized)
		dev_char_init();

	assert(dev);
	sem_pend(&s_chardev_lock);
	{
		// TODO: unregister on ref 0?
		--dev->base.refs;
	}
	sem_post(&s_chardev_lock);
}
