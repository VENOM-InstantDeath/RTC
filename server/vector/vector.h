#ifndef VECTOR_H
#define VECTOR_H
#include <stdio.h>
#include <stdlib.h>

struct vector {
	char** str;
	int size;
};

void vector_init(struct vector* sm);
int vector_add(struct vector* sm, char* str);
int vector_pop(struct vector* sm);
int vector_popat(struct vector* sm, int index);
void vector_free(struct vector* sm);

#endif
