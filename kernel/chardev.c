#include <chardev.h>
#include <errno.h>
#include <assert.h>

v_ilist char_devices = V_ILIST_INIT(char_devices);

// TODO: sleep locks on these
int read(struct char_dev *dev, char *buf, size_t n) {
	if (!dev->read)
		return -ENODEV;
	return dev->read(&dev->base, buf, n);
}

int write(struct char_dev *dev, char *buf, size_t n) {
	if (!dev->write)
		return -ENODEV;
	return dev->write(&dev->base, buf, n);
}

int chardev_register(struct char_dev *dev) {
	v_ilist_prepend(&dev->base.linkage, &char_devices);
	return 0;
}

int chardev_unregister(struct char_dev *dev) {
	v_ilist_remove(&dev->base.linkage);
	return 0;
}

struct char_dev *chardev_open(const char *name) {
	v_ilist *pos;
	v_ilist_for_each(pos, &char_devices) {
		struct device *d = v_ilist_get(pos, struct device, linkage);
		if (strcmp(d->name, name) == 0) {
			++d->refs;
			return (struct char_dev *)d;
		}
	}
	return NULL;
}

void chardev_close(struct char_dev *dev) {
	assert(dev);
	--dev->base.refs;
}
