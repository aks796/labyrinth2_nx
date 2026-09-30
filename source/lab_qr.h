/* lab_qr.h -- a QR code for a short text (lab_qr.c). MIT. */
#ifndef LAB_QR_H
#define LAB_QR_H
#include <stdint.h>

#define LAB_QR_MAX 57 /* version 10 */
/* modules: LAB_QR_MAX * LAB_QR_MAX bytes, row after row of *size (1 dark);
 * 0 on success, -1 if the text is too long (over 213 bytes) */
int lab_qr_encode(const char *text, uint8_t *modules, int *size);

#endif
