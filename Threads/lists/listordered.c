#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

extern void exit(int);        

#define BUFFER_SIZE 32 

// create a list node struct
struct version_node {
    struct list_elem elem;
    char name[BUFFER_SIZE];
    int major;
    int minor;
};

// create a list variable to hold your list nodes
struct list version_list;

// create a function for comparing two list nodes
// always needs to have these paramters
bool version_less_func(const struct list_elem *a, 
                       const struct list_elem *b, void *aux) {
	// get actual list nodes
    struct version_node *sa, *sb;
    sa = list_entry(a, struct version_node, elem);
    sb = list_entry(b, struct version_node, elem);

	// compare version numbers (major first, then minor)
    if (sa->major == sb->major) {
        return sa->minor < sb->minor;
    } else {
        return sa->major < sb->major;
    }
}

// prints out the list 
void print_version_list(struct list *l) {
    struct list_elem *e;
    printf("Versions:\n");
    for (e = list_begin(l); e != list_end(l); e = list_next(e)) {
        struct version_node *v = list_entry(e, struct version_node, elem);
        printf("%14s: %d.%d\n", v->name, v->major, v->minor);
    }
}

// test out list just created
int main(int argc, char **argv) {
    struct version_node *v;
    struct version_node v1, v2, v3, v4;

	// always initialize list
    list_init(&version_list);
    
	// set pointer to v1, and set v1's values
    v = &v1;
    strlcpy(v->name, "Liddy Piddy", BUFFER_SIZE);
    v->major = 3;
    v->minor = 1;

	// use list_insert_ordered to maintain a sorted list
	// requires poiner to comparison function
    list_insert_ordered(&version_list, &v->elem, version_less_func, NULL);

	// rinse and repeat
    v = &v2;
    strlcpy(v->name, "Maddy Paddy", BUFFER_SIZE);
    v->major = 3;
    v->minor = 2;
    list_insert_ordered(&version_list, &v->elem, version_less_func, NULL);

    v = &v3;
    strlcpy(v->name, "Jetty Ketty", BUFFER_SIZE);
    v->major = 2;
    v->minor = 4;
    list_insert_ordered(&version_list, &v->elem, version_less_func, NULL);

    v = &v4;
    strlcpy(v->name, "Zeldi Meldi", BUFFER_SIZE);
    v->major = 1;
    v->minor = 1;
    list_insert_ordered(&version_list, &v->elem, version_less_func, NULL);
       
	// print out sorted list
    print_version_list(&version_list);
    
    return 0;
}

void debug_panic(const char *file, int line, const char *function,
                 const char *message, ...) {
	va_list args;
	printf("Kernel PANIC at %s:%d in %s(): ", file, line, function);
	va_start(args, message);
	vprintf(message, args);
	printf("\n");
	va_end(args);
	exit(-1);
}

