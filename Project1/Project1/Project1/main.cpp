#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"
#include <math.h>
#include "wingdi.h"
#include <Windows.h>


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

double scale_real = (double)(REAL_MAX - REAL_MIN) / (double)DISPLAY_WIDTH;
double scale_image = (double)(IMAGE_MAX - IMAGE_MIN) / (double)DISPLAY_HEIGHT;

int result[PIXEL_CNT] = {};


struct Complex
{
	double real;
	double image;

	void set_value(double r, double i)
	{
		this->real = r;
		this->image = i;
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
	//printf("%lf %lf %lf %lf\n", z.real, z.image, c.real, c.image);
	do {
		temp = pow(z.real, 2) - pow(z.image, 2) + c.real;
		z.image = 2 * z.real * z.image + c.image;
		z.real = temp;
		length_sq = pow(z.real, 2) + pow(z.image, 2);
		count++;
	} while ((length_sq < 4.0) && (count < max));
	return count;
}

void SlaveProcess(int rank, int block_width)
{
	int color;
	int* data = (int*)malloc(block_width * (DISPLAY_HEIGHT+1) * sizeof(int));
	int* rows = (int*)malloc(block_width * sizeof(int));

	int offset;
	MPI_Status status;

	int recv_result = MPI_Recv(rows, block_width, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
	printf("Slave %d : BlockWidth = %d\n", rank, block_width);
	printf("\n");
	printf("Slave %d : row : ", rank);
	for (int i = 0; i < block_width; i++) {
		printf("%d ", rows[i]);
	}
	printf("\n");
	//while ((recv_result == MPI_SUCCESS) && (status.MPI_TAG == STATUS_CALC)) {
		printf("Slave %d : calculating color\n", rank);
		for (int i = 0; i < block_width; i++) {
			offset = DISPLAY_HEIGHT * i;
			data[offset] = rows[i];
			for (int col = 0; col < DISPLAY_HEIGHT; col++) {
				color = calc_pixel(col, rows[i]);
				int serialized_idx = offset + col + 1;
				data[serialized_idx] = color;
			}
		}
		MPI_Send(data, DISPLAY_HEIGHT * block_width, MPI_INT, 0, STATUS_DATA, MPI_COMM_WORLD);
	//}

	free(data);
	free(rows);
}

void MasterStaticProcess(int process_cnt, int block_width, FILE* output)
{
	int* rows = (int*)malloc(block_width * sizeof(int));
	int* data = (int*)malloc(block_width * DISPLAY_HEIGHT * sizeof(int));
	int task_running = 0;
	int row_cnt = 0;
	int offset;
	int process_id;

	MPI_Status status;

	if (rows == NULL) {
		printf("Master : allocation error\n");
		return;
	}
	else {
		printf("Master : allocation done\n");
	}
	for (int prc = 0; prc < process_cnt-1; prc++) {
		for (int i = 0; i < block_width; i++) {
			rows[i] = row_cnt++;
		}
		printf("Master : Send Rows\n");
		MPI_Send(rows, block_width, MPI_INT, prc + 1, STATUS_CALC, MPI_COMM_WORLD);
		task_running++;
	}
	printf("Master : generated task : %d\n", task_running);
	
	while (task_running > 0) {
		printf("Master : Receive Data\n");
		MPI_Recv(data, DISPLAY_HEIGHT * block_width, MPI_INT, MPI_ANY_SOURCE, STATUS_DATA, MPI_COMM_WORLD, &status);
		--task_running;
		process_id = status.MPI_SOURCE;
		MPI_Send(NULL, 0, MPI_INT, process_id, STATUS_SLEEP, MPI_COMM_WORLD);

		for (int i = 0; i < block_width; i++) {
			int row_num = data[DISPLAY_HEIGHT * i];
			int offset = row_num * DISPLAY_HEIGHT;
			int data_offset = DISPLAY_HEIGHT * i;
			for (int col = 0; col < DISPLAY_HEIGHT; ++col) {
				result[offset + col] = data[data_offset + col];
			}
		}
	}
	/*for (int i = 0; i < PIXEL_CNT; i++) {
		fprintf(output, "%d ", result[i]);
		if ((i+1) % 400 == 0) { fprintf(output, "\n"); }
	}*/
	free(data);
	free(rows);
}

int main(int argc, char* argv[])
{
	FILE* output = fopen("output.txt", "w");


	int np, me;
	MPI_Status status;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);
	if (np == 0)np = 1;
	int block_width = DISPLAY_WIDTH / (np-1);
	printf("Initialize : block width : %d\n", block_width);
	printf("Initialize : initialize complete\n");
	printf("Initialize : Real part scale : %lf\n", scale_real);
	printf("Initialize : image part scale : %lf\n", scale_image);

	if (me == 0) { // Master
		printf("Master : MasterProcessStart\n");
		MasterStaticProcess(np, block_width, output);
	}
	else { // Slave
		printf("Slave : SlaveProcessStart\n");
		SlaveProcess(me, block_width);
	}
	MPI_Finalize();
	fclose(output);
	exit(0);
}