#include "parser.h"

int parse_byte(parser_state* ps, char byte) {
    switch (ps->state) {
        case STATE_DOLLAR:
            if (byte == '$') {
                ps->state = STATE_TYPE;
                ps->index_type = 0;
            }
            break;
        case STATE_TYPE:
            if (byte == ',') {
                ps->state = STATE_PAYLOAD;
                ps->msg_type[ps->index_type] = '\0';
                ps->index_payload = 0; // initialize properly the index
            } else if (ps->index_type == 6) { // error! 
                ps->state = STATE_DOLLAR;
                ps->index_type = 0;
			} else if (byte == '*') {
				ps->state = STATE_DOLLAR; // get ready for a new message
                ps->msg_type[ps->index_type] = '\0';
				ps->msg_payload[0] = '\0'; // no payload
                return NEW_MESSAGE;
            } else {
                ps->msg_type[ps->index_type] = byte; // ok!
                ps->index_type++; // increment for the next time;
            }
            break;
        case STATE_PAYLOAD:
            if (byte == '*') {
                ps->state = STATE_DOLLAR; // get ready for a new message
                ps->msg_payload[ps->index_payload] = '\0';
                return NEW_MESSAGE;
            } else if (ps->index_payload == 100) { // error
                ps->state = STATE_DOLLAR;
                ps->index_payload = 0;
            } else {
                ps->msg_payload[ps->index_payload] = byte; // ok!
                ps->index_payload++; // increment for the next time;
            }
            break;
    }
    return NO_MESSAGE;
}

int extract_integer(const char* str) {
	int i = 0, number = 0, sign = 1;
	
	if (str[i] == '-') {
		sign = -1;
		i++;
	}
	else if (str[i] == '+') {
		sign = 1;
		i++;
	}
	while (str[i] != ',' && str[i] != '\0') {
		number *= 10; // multiply the current number by 10;
		number += str[i] - '0'; // converting character to decimal number
		i++;
	}
	return sign*number;
}		

int next_value(const char* msg, int i) {
	while (msg[i] != ',' && msg[i] != '\0') { i++; }
	if (msg[i] == ',')
		i++;
	return i;
}