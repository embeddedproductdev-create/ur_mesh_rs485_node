
#ifndef INC_JSON_MAKER_H_
#define INC_JSON_MAKER_H_

#include <stdint.h>

#define JWRITE_STACK_DEPTH 32 // max nesting depth of objects/arrays

#define JW_COMPACT 0 // output string control for jwOpen()
#define JW_PRETTY 1	 // pretty adds \n and indentation

enum jwNodeType
{
	JW_OBJECT = 1,
	JW_ARRAY
};

struct jwNodeStack
{
	enum jwNodeType nodeType;
	int elementNo;
};

typedef struct jWriteControl
{
	char *buffer;									  // pointer to application's buffer
	unsigned int buflen;							  // length of buffer
	char *bufp;										  // current write position in buffer
	char tmpbuf[32];								  // local buffer for int/double convertions
	int error;										  // error code
	int callNo;										  // API call on which error occurred
	struct jwNodeStack nodeStack[JWRITE_STACK_DEPTH]; // stack of array/object nodes
	int stackpos;
	int isPretty; // 1= pretty output (inserts \n and spaces)
} jWriteControl_t;
extern jWriteControl_t jwc;

// Error Codes
// -----------
#define JWRITE_OK 0
#define JWRITE_BUF_FULL 1	 // output buffer full
#define JWRITE_NOT_ARRAY 2	 // tried to write Array value into Object
#define JWRITE_NOT_OBJECT 3	 // tried to write Object key/value into Array
#define JWRITE_STACK_FULL 4	 // array/object nesting > JWRITE_STACK_DEPTH
#define JWRITE_STACK_EMPTY 5 // stack underflow error (too many 'end's)
#define JWRITE_NEST_ERROR 6	 // nesting error, not all objects closed when jwClose() called

char *jwErrorToString(int err);

#ifdef __cplusplus
extern "C" {
#endif

void jwOpen(struct jWriteControl *jwc, char *buffer, unsigned int buflen, enum jwNodeType rootType, int isPretty);
int jwClose(struct jWriteControl *jwc);
int jwErrorPos(struct jWriteControl *jwc);
void jwObj_string(struct jWriteControl *jwc, const char *key, char *value);

void jwObj_int(struct jWriteControl *jwc, const char *key, int value);
void jwObj_long_int(struct jWriteControl *jwc, char *key, int64_t value);

void jwObj_double(struct jWriteControl *jwc, char *key, double value);
void jwObj_bool(struct jWriteControl *jwc, char *key, int oneOrZero);
void jwObj_null(struct jWriteControl *jwc, char *key);
void jwObj_object(struct jWriteControl *jwc, char *key);
void jwObj_array(struct jWriteControl *jwc, const char *key);
void jwArr_string(struct jWriteControl *jwc, char *value);
void jwArr_int(struct jWriteControl *jwc, int value);
void jwArr_double(struct jWriteControl *jwc, double value);
void jwArr_bool(struct jWriteControl *jwc, int oneOrZero);
void jwArr_null(struct jWriteControl *jwc);
void jwArr_object(struct jWriteControl *jwc);
void jwArr_array(struct jWriteControl *jwc);
int jwEnd(struct jWriteControl *jwc);
void jwObj_raw(struct jWriteControl *jwc, const char *key, char *rawtext);
void jwArr_raw(struct jWriteControl *jwc, char *rawtext);

#ifdef __cplusplus
}
#endif

#endif

