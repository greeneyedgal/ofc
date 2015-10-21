#ifndef __ofc_sema_array_h__
#define __ofc_sema_array_h__

typedef struct
{
	unsigned dimensions;
	unsigned size[0];
} ofc_sema_array_t;

ofc_sema_array_t* ofc_sema_array_create(
	unsigned dimensions,
	unsigned* base, unsigned* count);

void ofc_sema_array_delete(ofc_sema_array_t* array);

uint8_t ofc_sema_array_hash(
	const ofc_sema_array_t* array);
bool ofc_sema_array_compare(
	const ofc_sema_array_t* a,
	const ofc_sema_array_t* b);

unsigned ofc_sema_array_total(const ofc_sema_array_t* array);

#endif