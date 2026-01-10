#ifndef GNM_PLUGIN_EXCEL_RC4_H_
#define GNM_PLUGIN_EXCEL_RC4_H_

 /* rc4.h */
typedef struct {
	unsigned char state[256];
	unsigned char x;
	unsigned char y;
} RC4_KEY;
void prepare_key (unsigned char *key_data_ptr, int key_data_len, RC4_KEY *key);
void rc4 (unsigned char *buffer_ptr, unsigned buffer_len, RC4_KEY *key);

#endif
