#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vector.h"

void vector_init(struct vector* sm) {
	sm->str = malloc(51);
	sm->size=0;
}

int vector_add(struct vector* sm, char* str) {
	int size = strlen(str);
	if (size > 50) {return -1;}
	sm->str = realloc(sm->str, 51*sm->size+1);
	sm->str[sm->size] = str;
	sm->size++;
	return 0;
}

int vector_pop(struct vector* sm) {
	if (sm->size == 0) {return -1;}
	sm->size--;
	sm->str = realloc(sm->str, 51*sm->size+1);
	return 0;
}

int vector_popat(struct vector* sm, int index) {
	if (sm->size == 0) {return -1;}
	sm->size--;
	//Continue from here
	return 0;
}

void vector_free(struct vector* sm) {free(sm->str);}
