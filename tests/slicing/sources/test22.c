#include <stddef.h>
#include <stdlib.h>

struct ldv_kref {
    int refcount;
};

struct ldv_kobject {
    int a;
    struct ldv_kref kref;
};

static void ldv_kobject_cleanup(struct ldv_kobject *kobj) {
    test_assert(kobj->a == 0xdead);
}

static void ldv_kobject_release(struct ldv_kref *kref) {
    struct ldv_kobject *kobj = ({
        const typeof(((struct ldv_kobject *) 0)->kref) *__mptr = (kref);
        (struct ldv_kobject *) ((char *) __mptr -
                                offsetof(struct ldv_kobject, kref));
    });
    ldv_kobject_cleanup(kobj);
}

static void ldv_kref_sub(struct ldv_kref *kref, unsigned int count,
                         void (*release)(struct ldv_kref *kref)) {
    release(kref);
}

int main(void) {
    struct ldv_kobject *kobj;

    kobj = malloc(sizeof(*kobj));
    kobj->a = 0xdead;
    ldv_kref_sub(&kobj->kref, 1, ldv_kobject_release);
    return 0;
}
