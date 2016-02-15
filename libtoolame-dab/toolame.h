
void global_init (void);
void proginfo (void);
void short_usage (void);

void obtain_parameters (frame_info *, int *, unsigned long *,
			       char[MAX_NAME_SIZE], char[MAX_NAME_SIZE]);
void parse_args (int, char **, frame_info *, int *, unsigned long *,
			char[MAX_NAME_SIZE], char[MAX_NAME_SIZE], char**, char**);
void print_config (frame_info *, int *,
			  char[MAX_NAME_SIZE], char[MAX_NAME_SIZE]);
void usage (void);


void smr_dump(double smr[2][SBLIMIT], int nch);

