#include <fs/dev_char.h>
#include <errno.h>
#include <assert.h>

static v_ilist s_char_devices = V_ILIST_INIT(s_char_devices);
static SEMAPHORE(s_sem_char_devices, 1);

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

int dev_char_register(struct dev_char *dev) {
	sem_pend(&s_sem_char_devices);
	{
		v_ilist_prepend(&dev->base.linkage, &s_char_devices);
	}
	sem_post(&s_sem_char_devices);
	return 0;
}

int dev_char_unregister(struct dev_char *dev) {
	sem_pend(&s_sem_char_devices);
	{
		v_ilist_remove(&dev->base.linkage);
		// FIXME: free/deinit
	}
	sem_post(&s_sem_char_devices);
	return 0;
}

struct dev_char *dev_char_open(const char *name) {
	struct dev_char *device = NULL;
	sem_pend(&s_sem_char_devices);
	{
		v_ilist *pos;
		v_ilist_for_each(pos, &s_char_devices) {
			struct device *d = v_ilist_get(pos, struct device, linkage);
			if (strcmp(d->name, name) == 0) {
				++d->refs;
				device = (struct dev_char *)d;
				break;
			}
		}
	}
	sem_post(&s_sem_char_devices);
	return device;
}

void dev_char_close(struct dev_char *dev) {
	assert(dev);
	sem_pend(&s_sem_char_devices);
	{
		// TODO: unregister on ref 0?
		--dev->base.refs;
	}
	sem_post(&s_sem_char_devices);
}
