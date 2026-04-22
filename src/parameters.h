#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <stdint.h>
//#include <stdbool.h>

#define MAX_NETS      15
#define MAX_ROUTES    10

typedef struct {
	unsigned char net;
	unsigned char path;
	unsigned char dau;
	unsigned char follow;
	unsigned char error;
	unsigned char revers;
} tci_routes;

typedef struct {
	unsigned char primary_net;
	unsigned char next_time;
	unsigned char next_rpt;
	unsigned char error_rpt;
	unsigned char pretime;
	unsigned char deadtime;
	unsigned char sys_tok;
	unsigned char netdau[MAX_NETS];
	tci_routes    route[MAX_ROUTES];
} tci_parameters;

extern tci_parameters param;

typedef struct {
	unsigned char follow;	// Adresat (DAU) v prime ceste
	unsigned char error;	// Adresat (DAU) v chybove ceste
	unsigned char revers;	// Adresat (DAU) v reverzni ceste
} POCSAG_route;

extern POCSAG_route route;  // definuje routu pro vysilani


void Parameters_Init(void);
void Parameters_Show(void);
void make_route(unsigned char net, unsigned char path, unsigned char dau);

#endif
