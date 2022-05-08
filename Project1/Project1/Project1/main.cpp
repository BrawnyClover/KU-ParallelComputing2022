#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"
#include <math.h>

#pragma warning(disable:4996)

#define STATUS_CALC 1
#define STATUS_DATA 2
#define STATUS_SLEEP 3

const int DISPLAY_WIDTH = 400;
const int DISPLAY_HEIGHT = 400;
const int PIXEL_CNT = 160000;

const int REAL_MAX = 2;
const int REAL_MIN = -2;

const int IMAGE_MAX = 2;
const int IMAGE_MIN = -2;

double scale_real = (REAL_MAX - REAL_MIN) / DISPLAY_WIDTH;
double scale_image = (IMAGE_MAX - IMAGE_MIN) / DISPLAY_HEIGHT;

int result[PIXEL_CNT] = {};

struct Complex
{
	double real;
	double image;

	void set_value(double r, double i)
	{
		real = r;
		image = i;
	}
};

int calc_pixel(int x, int y)
{
	int count, max;
	Complex c, z;
	double temp, length_sq;
	count = 0;
	max = 256;
	z.set_value(0, 0);
	c.set_value(
		REAL_MIN + ((double)x * scale_real),
		IMAGE_MIN + ((double)y * scale_image)
	);
	do {
		temp = pow(z.real, 2) - pow(z.image, 2) + c.real;
		z.image = 2 * z.real * z.image + c.image;
		z.real = temp;
		length_sq = pow(z.real, 2) + pow(z.image, 2);
		count++;
	} while ((length_sq < 4.0) && (count < max));
	return count;  
}

void SlaveProcess(int block_width)
{
	int color;
	int* data = (int*)malloc(block_width * DISPLAY_HEIGHT * sizeof(int));
	int* rows = (int*)malloc(block_width * sizeof(int));

	int offset;
	MPI_Status status;

	int recv_result = MPI_Recv(rows, block_width, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
	printf("received rows\n");
	while ((recv_result == MPI_SUCCESS) && (status.MPI_TAG == STATUS_CALC)) {
		for (int i = 0; i < block_width; i++) {
			offset = DISPLAY_HEIGHT * i;
			data[offset] = rows[i];

			/* compute pixel colors using mandelbrot algorithm */
			for (int col = 0; col < DISPLAY_HEIGHT; ++col) {
				color = calc_pixel(col, rows[i]);
				data[offset + col + 1] = color;
			}
		}

		/* send row(s) to master */
		MPI_Send(data, DISPLAY_HEIGHT * block_width, MPI_INT, 0, STATUS_DATA, MPI_COMM_WORLD);
	}

	free(data);
	free(rows);
}

void MasterProcess(int process_cnt, int block_width)
{
	int* rows = (int*)malloc(block_width * sizeof(int));
	int* data = (int*)malloc(block_width * DISPLAY_HEIGHT * sizeof(int));
	int task_running = 0;
	int row_cnt = 0;
	int offset;
	int process_id;



	MPI_Status status;

	for (int prc = 0; prc < process_cnt-1; prc++) {
		for (int i = 0; i < block_width; i++) {
			rows[i] = row_cnt++;
		}
		MPI_Send(rows, block_width, MPI_INT, prc+1, STATUS_CALC, MPI_COMM_WORLD);
		task_running++;
	}

	while (task_running > 0) {
		MPI_Recv(data, DISPLAY_HEIGHT * block_width, MPI_INT, MPI_ANY_SOURCE, STATUS_DATA, MPI_COMM_WORLD, &status);
		--task_running;
		process_id = status.MPI_SOURCE;

		/* if there are still rows to be processed, send slave to work again
		 * otherwise send him to sleep */
		if (row_cnt < DISPLAY_HEIGHT) {
			for (int i = 0; i < block_width; ++i) {
				rows[i] = row_cnt++;
			}
			MPI_Send(rows, block_width, MPI_INT, process_id, STATUS_CALC, MPI_COMM_WORLD);
			task_running++;
		}
		else {
			MPI_Send(NULL, 0, MPI_INT, process_id, STATUS_SLEEP, MPI_COMM_WORLD);
		}

		/* store received row(s) in rgb buffer */
		for (int i = 0; i < block_width; ++i) {
			offset = DISPLAY_HEIGHT * i;

			for (int col = 0; col < DISPLAY_HEIGHT; ++col) {
				result[offset + col + 1] = data[offset + col + 1];
			}
		}
	}
	for (int i = 0; i < PIXEL_CNT; i++) {
		printf("%d ", result[i]);
		if (i % 400 == 0) { printf("\n"); }
	}
	free(data);
	free(rows);
}

int main(int argc, char* argv[])
{
	freopen("output.txt", "w", stdout);
	const int K = 1024;
	const int msgsize = 256 * K; /* Messages will be of size 1MB */
	int np, me, i;
	int* X, * Y;
	int tag = 42;
	MPI_Status status;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);
	/* Check that we run on exactly two processes */
	if (np != 2) {
		if (me == 0) {
			printf("You have to use exactly 2 processes to run this program \n");
		}
		MPI_Finalize();
		exit(0); /* Quit if there is only one process*/
	}
	/* Allocate memory for large message buffers */
	X = (int*)malloc(msgsize * sizeof(int));
	Y = (int*)malloc(msgsize * sizeof(int));
	/* Initialize X and Y */

	int block_width = DISPLAY_WIDTH / np;

	if (me == 0) { // Master
		MasterProcess(np, block_width);
	}
	else { // Slave
		SlaveProcess(block_width);
	}
	MPI_Finalize();
	exit(0);
}