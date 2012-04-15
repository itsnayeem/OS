/* 
 * Example of using Pintos's list implementation.
 * See list.c and list.c in src/lib/kernel for more.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

extern void exit(int);        

#define BUFFER_SIZE 128

// create a node struct
struct server_node {
	// must always have a list_elem
	// list_elem contains prev/next points
    struct list_elem elem;

	// may have any additional fields you want
    char name[BUFFER_SIZE];
};

// create a list (list struct contains head/tail pointers)
struct list server_list;

// prints list (demonstrates list iteration)
void print_server_list(struct list *l) {
    struct list_elem *e;

    printf("Servers: ");

	// use list_elem and list_begin()/list_next()/list_end() to iterate
    for (e = list_begin(l); e != list_end(l); e = list_next(e)) {
		// use list_entry to get actual node
        struct server_node *s = list_entry(e, struct server_node, elem);
        printf("%s ", s->name);
    }
    printf("\n");
}

// returns server node if found, otherwise null
struct server_node *find_server(struct list *l, char *name) {
    struct list_elem *e;
    struct server_node *found = NULL;
   
   	// same iteration as before
    for (e = list_begin(l); e != list_end(l); e = list_next(e)) {
		// use list_entry to get actual node
        struct server_node *s = list_entry(e, struct server_node, elem);

		// test if the names are the same
        if (strcmp(s->name, name) == 0) {
            found = s;
            break;
        }
    }
    
    return found;
}

// test out your list
int main(int argc, char **argv) {
	// create a pointer to a server node
    struct server_node *s;

	// create actual server nodes (allocates space)
    struct server_node s1, s2, s3, s4;

	// always call list_init to initialize your list
    list_init(&server_list);
   
   	// point to the first node
    s = &s1;

	// set name for server node
	// notice use of strlcpy instead of strcpy or strncpy
    strlcpy(s->name, "Foo", BUFFER_SIZE);

	// place node at the back of the list
    list_push_back(&server_list, &s->elem);
    
	// rinse and repeat
    s = &s2;
    strlcpy(s->name, "Goo", BUFFER_SIZE);
    list_push_back(&server_list, &s->elem);

    s = &s3;
    strlcpy(s->name, "Boo", BUFFER_SIZE);
    list_push_back(&server_list, &s->elem);

    s = &s4;
    strlcpy(s->name, "Zoo", BUFFER_SIZE);
    list_push_back(&server_list, &s->elem);
        
	// print out the list
    print_server_list(&server_list);

	// find and remove a node
    if ((s = find_server(&server_list, "Boo")) != NULL) {
        printf("Found Boo, removing.\n");

		// remove nodes with list_remove
        list_remove(&s->elem);
    }

    print_server_list(&server_list);
    
    return 0;
}

// more on this later
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
