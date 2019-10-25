typedef unsigned long size_t;
extern void *malloc(size_t);

struct ldv_kref {
	int refcount;
};

struct ldv_kobject {
	int a;
        struct ldv_kref kref;
};

static void ldv_kobject_cleanup(struct ldv_kobject *kobj)
{
        test_assert(kobj == 0);
}

static void ldv_kobject_release(struct ldv_kref *kref) {
        ldv_kobject_cleanup((void*) kref);
}


static void ldv_kref_sub(unsigned int count,
                         void (*release)(struct ldv_kref *kref))
{
	release(0);
}

int main(void) {
        struct ldv_kobject *kobj;

        ldv_kref_sub(1, ldv_kobject_release);
        return 0;
}
