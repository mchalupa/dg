#include <stddef.h>
#include <stdlib.h>

typedef struct ldv_kref {
    int refcount;
} ldv_kref_t;

typedef struct ldv_kobject {
    int a;
    struct ldv_kref kref;
} ldv_kobject_t;

static void ldv_kobject_cleanup(ldv_kobject_t *kobj) {
    test_assert(kobj->a == 0xdead);
}

static void ldv_kobject_release(ldv_kref_t *kref) {
    ldv_kobject_t *kobj = ({
        typeof(((ldv_kobject_t *) 0)->kref) *mptr = kref;
        (ldv_kobject_t *) ((char *) mptr - offsetof(ldv_kobject_t, kref));
    });
    void (*func)(ldv_kobject_t *) = ldv_kobject_cleanup;
    func(kobj);
}

static void ldv_kref_sub(ldv_kref_t *kref, unsigned int count,
                         void (*release)(ldv_kref_t *kref)) {
    release(kref);
}

/* derived from test22.c, adds one more indirect call */
int main(void) {
    ldv_kobject_t *kobj;

    kobj = malloc(sizeof(*kobj));
    kobj->a = 0xdead;
    ldv_kref_sub(&kobj->kref, 1, ldv_kobject_release);
    return 0;
}
