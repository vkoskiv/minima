int read(int fd, void *buf, unsigned count);
int write(int fd, const void *buf, unsigned count);

char getchar(void);
// FIXME: These need to work with a FILE* I think?
int getline(char *out, unsigned n);
