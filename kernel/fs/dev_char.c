#include <fs/dev_char.h>
#include <errno.h>
#include <assert.h>

v_ilist char_devices = V_ILIST_INIT(char_devices);

// TODO: sleep locks on these
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
	v_ilist_prepend(&dev->base.linkage, &char_devices);
	return 0;
}

int dev_char_unregister(struct dev_char *dev) {
	v_ilist_remove(&dev->base.linkage);
	return 0;
}

struct dev_char *dev_char_open(const char *name) {
	v_ilist *pos;
	v_ilist_for_each(pos, &char_devices) {
		struct device *d = v_ilist_get(pos, struct device, linkage);
		if (strcmp(d->name, name) == 0) {
			++d->refs;
			return (struct dev_char *)d;
		}
	}
	return NULL;
}

void dev_char_close(struct dev_char *dev) {
	assert(dev);
	--dev->base.refs;
}
