// C standard header files
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// other header files
#include <mpi.h>

const int RULE_MEMBERS = 5;
const char BLACK = '#';
const char WHITE = '-';

typedef struct Config {
	int columns;
	int rows;
	int rulesCount;
	int* rules;
} Config;

typedef struct Handle {
	char* configFile;
	bool help;
	bool verbose;
} Handle;

char* createRectangle(Config* config);
void printConfig(const Config* config);
void printRectangle(const char* rectangle, const int rows, const int columns);
Handle processParameters(int argc, char* argv[]);
void readConfig(Config* config, const char inputFileName[]);
int* search(const char* rectangle, const int rows, const int columns);

/*
 * main program
 */
int main(int argc, char* argv[]) {
	// MPI variables and initialization
	int size;
	int rank;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Datatype MPI_HANDLE;
	int blockCounts[2] = { 1, 2 };
	MPI_Aint width[2];
	MPI_Type_extent(MPI_UNSIGNED, &width[0]);
	MPI_Type_extent(MPI_C_BOOL, &width[1]);
	MPI_Aint offsets[2] = { 0, 1 * width[0] };
	MPI_Datatype oldTypes[2] = { MPI_UNSIGNED, MPI_C_BOOL };
	MPI_Type_struct(2, blockCounts, offsets, oldTypes, &MPI_HANDLE);
	MPI_Type_commit(&MPI_HANDLE);

	Config* config;
	Handle handle;
	char* rectangle = NULL;
	int rows;
	int columns;

	if (rank == 0) {
		// process command line parameters
		handle = processParameters(argc, argv);
	}

	MPI_Bcast(&handle, 1, MPI_HANDLE, 0, MPI_COMM_WORLD);
	if (handle.help) {
		MPI_Finalize();
		exit(EXIT_SUCCESS);
	}

	if (rank == 0) {
		printf("Starting\n");

		config = &(Config ) { .columns = 0, .rows = 0, .rules = NULL };
		readConfig(config, handle.configFile);
		rows = config->rows;
		columns = config->columns;

		if (handle.verbose) {
			printf("Configuration:\n");
			printConfig(config);
		}

		rectangle = createRectangle(config);

		if (handle.verbose) {
			printf("Rectangle:\n");
			printRectangle(rectangle, rows, columns);
		}
	}

	double start = MPI_Wtime();
	MPI_Bcast(&rows, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&columns, 1, MPI_INT, 0, MPI_COMM_WORLD);

	int rowsPart = (rows + size - 1) / size;

	char* rectanglePart = (char*) malloc(rowsPart * columns * sizeof(char));

	MPI_Scatter(rectangle, rowsPart * columns, MPI_CHAR, rectanglePart,
			rowsPart * columns, MPI_CHAR, 0, MPI_COMM_WORLD);

	if (rank == size - 1 && rows % rowsPart != 0) {
		// number of elements and processes isn't divisible without remainder
		rowsPart = rows % rowsPart;
	}

	int* resultPart = search(rectanglePart, rowsPart, columns);

	int results[size * RULE_MEMBERS];
	MPI_Gather(resultPart, RULE_MEMBERS, MPI_INT, results, RULE_MEMBERS,
	MPI_INT, 0, MPI_COMM_WORLD);

	if (rank == 0) {
		// add offset to the rows
		for (int i = 0; i < size; i++) {
			results[i * RULE_MEMBERS + 1] += i * rowsPart;
			results[i * RULE_MEMBERS + 3] += i * rowsPart;
		}

		int result[RULE_MEMBERS];
		result[0] = 0;
		result[1] = INT_MIN;
		result[2] = INT_MIN;
		result[3] = INT_MAX;
		result[4] = INT_MAX;
		bool breaked = false;
		bool nextIsWrong = false;
		for (int i = 0; i < size; i++) {
			if (results[i * RULE_MEMBERS] == 2) {
				// more then one black rectangle
				breaked = true;
				break;
			} else if (results[i * RULE_MEMBERS] == 1) {
				if (result[0] == 0) {
					// first black rectangle found
					for (int j = 0; j < RULE_MEMBERS; j++) {
						result[j] = results[i * RULE_MEMBERS + j];
					}
				} else {
					// all other rectangles
					if (nextIsWrong
							|| (result[0] == 1
									&& result[3] + 1
											!= results[i * RULE_MEMBERS + 1])) {
						// gap between two black rectangles
						breaked = true;
						break;
					} else if (result[2] != results[i * RULE_MEMBERS + 2]
							|| result[4] != results[i * RULE_MEMBERS + 4]) {
						// vertical shifted rectangles
						breaked = true;
						break;
					} else {
						result[3] = results[i * RULE_MEMBERS + 3];
						result[4] = results[i * RULE_MEMBERS + 4];
					}
				}
				if ((results[i * RULE_MEMBERS + 3] + 1) % rowsPart != 0) {
					// black rectangle isn't at the end of the part
					nextIsWrong = true;
				}
			}
		}
		if (breaked) {
			result[0] = 2;
		}

		printf("Time elapsed: %f s\n", MPI_Wtime() - start);

		printf("Final result:\n");
		for (int i = 0; i < RULE_MEMBERS; i++) {
			printf("%d ", result[i]);
		}
		printf("\n");

		if (handle.verbose) {
			if (result[0] == 0) {
				printf("No black rectangle!\n");
			} else if (result[0] == 1) {
				printf("One black rectangle!\n"
						"Coordinates:\n");
				for (int i = 1; i < RULE_MEMBERS; i++) {
					printf("%d ", result[i]);
				}
				printf("\n");
			} else if (result[0] == 2) {
				printf("More then one black rectangle!\n");
			}
		}
	}

	free(resultPart);

	if (rank == 0) {
		free(config->rules);
		free(rectangle);

		printf("Finished\n");
	}

	MPI_Finalize();

	return EXIT_SUCCESS;
}

/*
 * create rectangle from configuration
 */
char* createRectangle(Config* config) {
	char* rectangle = (char*) malloc(
			config->columns * config->rows * sizeof(char));
	for (int n = 0; n < config->rulesCount; n++) {
		switch (config->rules[n * RULE_MEMBERS]) {
		case 0:
			// white
			for (int i = config->rules[n * RULE_MEMBERS + 1];
					i <= config->rules[n * RULE_MEMBERS + 3]; i++) {
				for (int j = config->rules[n * RULE_MEMBERS + 2];
						j <= config->rules[n * RULE_MEMBERS + 4]; j++) {
					rectangle[i * config->columns + j] = WHITE;
				}
			}
			break;
		case 1:
			// black
			for (int i = config->rules[n * RULE_MEMBERS + 1];
					i <= config->rules[n * RULE_MEMBERS + 3]; i++) {
				for (int j = config->rules[n * RULE_MEMBERS + 2];
						j <= config->rules[n * RULE_MEMBERS + 4]; j++) {
					rectangle[i * config->columns + j] = BLACK;
				}
			}
			break;
		case 2:
			// toggle
			for (int i = config->rules[n * RULE_MEMBERS + 1];
					i <= config->rules[n * RULE_MEMBERS + 3]; i++) {
				for (int j = config->rules[n * RULE_MEMBERS + 2];
						j <= config->rules[n * RULE_MEMBERS + 4]; j++) {
					if (rectangle[i * config->columns + j] == WHITE) {
						rectangle[i * config->columns + j] = BLACK;
					} else {
						rectangle[i * config->columns + j] = WHITE;
					}
				}
			}
			break;
		default:
			break;
		}
	}
	return rectangle;
}

/*
 * print the configuration
 */
void printConfig(const Config* config) {
	printf("%d %d\n", config->rows, config->columns);
	printf("%d\n", config->rulesCount);
	for (int i = 0; i < config->rulesCount; i++) {
		for (int j = 0; j < RULE_MEMBERS; j++) {
			printf("%d ", config->rules[i * RULE_MEMBERS + j]);
		}
		printf("\n");
	}
}

/*
 * print the rectangle
 */
void printRectangle(const char* rectangle, const int rows, const int columns) {
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns; j++) {
			printf("%c", rectangle[i * columns + j]);
		}
		printf("\n");
	}
}

/*
 * process the command line parameters and return a Handle struct with them
 */
Handle processParameters(int argc, char* argv[]) {
	Handle handle = { };

	for (int currentArgument = 1; currentArgument < argc; currentArgument++) {
		switch (argv[currentArgument][1]) {
		case 'f':
			// set configuration file location
			handle.configFile = &argv[currentArgument + 1][0];
			currentArgument++;
			break;
		case 'h':
			// print help message
			printf(
					"Parameters:\n"
							"\t-f <file>\tconfiguration file location\n"
							"\t-h\t\tprint this help message\n"
							"\t-v\t\tprint more information\n"
							"\nThis program is distributed under the terms of the LGPLv3 license\n");
			handle.help = true;
			break;
		case 'v':
			// print more information
			handle.verbose = true;
			break;
		default:
			fprintf(stderr, "Wrong parameter: %s\n"
					"-h for help\n", argv[currentArgument]);
			exit(EXIT_FAILURE);
		}
	}

	return handle;
}

/*
 * read the specified configuration file
 */
void readConfig(Config* config, const char inputFileName[]) {
	// open the file
	FILE* inputFile;
	const char* inputMode = "r";
	inputFile = fopen(inputFileName, inputMode);
	if (inputFile == NULL) {
		fprintf(stderr, "Couldn't open input file, exiting!\n");
		exit(EXIT_FAILURE);
	}

	// first line contains number of rows and columns
	int fscanfResult = fscanf(inputFile, "%d %d", &config->rows,
			&config->columns);

	// second line contains number of entries
	fscanfResult = fscanf(inputFile, "%d", &config->rulesCount);
	config->rules = (int*) malloc(
			RULE_MEMBERS * config->rulesCount * sizeof(int));

	// all lines after the first contain the entries, values stored as "type r1 c1 r2 c2"
	int type, r1, c1, r2, c2;
	for (int i = 0; i < config->rulesCount; i++) {
		fscanfResult = fscanf(inputFile, "%d %d %d %d %d", &type, &r1, &c1, &r2,
				&c2);

		// EOF result of *scanf indicates an error
		if (fscanfResult == EOF) {
			fprintf(stderr,
					"Something went wrong during reading of configuration file, exiting!\n");
			fclose(inputFile);
			exit(EXIT_FAILURE);
		}

		config->rules[i * RULE_MEMBERS] = type;
		config->rules[i * RULE_MEMBERS + 1] = r1;
		config->rules[i * RULE_MEMBERS + 2] = c1;
		config->rules[i * RULE_MEMBERS + 3] = r2;
		config->rules[i * RULE_MEMBERS + 4] = c2;
	}

	fclose(inputFile);
}

/*
 * search for black rectangles
 */
int* search(const char* rectangle, const int rows, const int columns) {
	int* result = (int*) malloc(RULE_MEMBERS * sizeof(int));
	result[0] = -1;
	result[1] = INT_MIN;
	result[2] = INT_MIN;
	result[3] = INT_MAX;
	result[4] = INT_MAX;
	bool foundStart = false;
	bool foundRowEnd = false;
	bool foundColumnEnd = false;
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns; j++) {
			if (rectangle[i * columns + j] == WHITE) {
				// white field
				if (foundStart && j >= result[2]) {
					if (foundColumnEnd) {
						if (j <= result[4]) {
							if (foundRowEnd) {
								if (i <= result[3]) {
									goto moreThenOne;
								}
							} else {
								if (j == result[2]) {
									// white field under first black field in previous row
									result[3] = i - 1;
									foundRowEnd = true;
								} else {
									goto moreThenOne;
								}
							}
						}
					} else {
						if (i == result[1]) {
							// same row as first black field, column end found
							result[4] = j - 1;
							foundColumnEnd = true;
						} else {
							// last element of previous row was a black field
							result[3] = i - 1;
							result[4] = columns - 1;
							foundColumnEnd = true;
							foundRowEnd = true;
						}
					}
				}
			} else {
				// black field
				if (foundStart) {
					if (j >= result[2]) {
						if (j == 0 && !foundColumnEnd) {
							// previous row ended with black field
							result[4] = columns - 1;
							foundColumnEnd = true;
						} else if (foundColumnEnd) {
							if (j <= result[4]) {
								if (foundRowEnd) {
									if (i <= result[3]) {
										// inside of valid region
									} else {
										goto moreThenOne;
									}
								}
							} else {
								goto moreThenOne;
							}
						}
					} else {
						goto moreThenOne;
					}
				} else {
					// first black field, rectangle start found
					result[0] = 1;
					result[1] = i;
					result[2] = j;
					foundStart = true;
				}
			}
		}
	}
	if (!foundRowEnd) {
		if (rectangle[rows * columns - 1] == BLACK) {
			// valid and last field is black
			result[3] = rows - 1;
			result[4] = columns - 1;
			foundColumnEnd = true;
			foundRowEnd = true;
		}
	}
	if (!foundRowEnd) {
		if (foundStart && foundColumnEnd) {
			// last row end has valid black fields
			result[3] = rows - 1;
			foundRowEnd = true;
		}
	}
	if (!foundStart) {
		// no black field
		result[0] = 0;
	}

	return result;

	moreThenOne: result[0] = 2;
	return result;
}
