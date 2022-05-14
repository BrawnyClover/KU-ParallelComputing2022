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
const int PIXEL_CNT = DISPLAY_WIDTH * DISPLAY_HEIGHT;
const int DATA_CNT = (DISPLAY_WIDTH) * (DISPLAY_HEIGHT + 1);

const int COMPLEX_SPACE = 2;

const int REAL_MAX = COMPLEX_SPACE;
const int REAL_MIN = -COMPLEX_SPACE;

const int IMAGE_MAX = COMPLEX_SPACE;
const int IMAGE_MIN = -COMPLEX_SPACE;

const int N_rectanle = 40;

const int FILE_HEADER_SIZE = 14;
const int INFO_HEADER_SIZE = 40;

bool isStatic;

const double scale_real = (double)(REAL_MAX - REAL_MIN) / (double)DISPLAY_WIDTH;
const double scale_image = (double)(IMAGE_MAX - IMAGE_MIN) / (double)DISPLAY_HEIGHT;

unsigned char image[DISPLAY_HEIGHT][DISPLAY_WIDTH][3];

int result[DATA_CNT] = {};

int calc_pixel(int x, int y);
void SlaveProcess(int rank, int block_width);
void MasterProcess(int process_cnt, int block_width);
void generateBitmapImage();
unsigned char* createBitmapFileHeader(int height, int stride);
unsigned char* createBitmapInfoHeader(int height, int width);

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
	int* data = (int*)malloc(block_width * (DISPLAY_HEIGHT + 1) * sizeof(int));
	int* rows = (int*)malloc(block_width * sizeof(int));

	int offset;
	MPI_Status status;

	int recv_result;
	do {
		recv_result = MPI_Recv(rows, block_width, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		printf("Slave %d : calculating color from row %d to %d ", rank, rows[0], rows[block_width - 1]);
		for (int i = 0; i < block_width; i++) {
			offset = (DISPLAY_HEIGHT + 1) * i;
			data[offset] = rows[i];
			int serialized_idx;
			for (int col = 0; col < DISPLAY_HEIGHT; ++col) {
				color = calc_pixel(col, rows[i]);
				serialized_idx = offset + col + 1;
				data[serialized_idx] = color;
			}
		}

		printf("...done, send data to master\n", rank);
		MPI_Send(data, (DISPLAY_HEIGHT + 1) * block_width, MPI_INT, 0, STATUS_DATA, MPI_COMM_WORLD);
	} while ((recv_result == MPI_SUCCESS) && (status.MPI_TAG == STATUS_CALC) && isStatic == false);

	free(data);
	free(rows);
}

void MasterProcess(int process_cnt, int block_width)
{
	int* rows = (int*)malloc(block_width * sizeof(int));
	int* data = (int*)malloc(block_width * (DISPLAY_HEIGHT + 1) * sizeof(int));
	int task_running = 0;
	int row_cnt = 0;
	int process_id;

	MPI_Status status;

	if (rows == NULL) {
		printf("Master : allocation error\n");
		return;
	}
	else {
		printf("Master : allocation done\n");
	}

	printf("Master : generateing initial tasks\n");
	for (int prc = 0; prc < process_cnt - 1; prc++) {
		for (int i = 0; i < block_width; i++) {
			rows[i] = row_cnt++;
		}
		MPI_Send(rows, block_width, MPI_INT, prc + 1, STATUS_CALC, MPI_COMM_WORLD);
		task_running++;
		printf("Master : Sending Rows, from %d to %d, tasks remain : %d\n", rows[0], rows[block_width - 1], task_running);
	}
	

	while (task_running > 0) {
		MPI_Recv(data, (DISPLAY_HEIGHT + 1) * block_width, MPI_INT, MPI_ANY_SOURCE, STATUS_DATA, MPI_COMM_WORLD, &status);
		--task_running;
		process_id = status.MPI_SOURCE;
		//printf("%dMaster : Received Data from slave \n", process_id);
		printf("0\n");
		if (isStatic) {
			MPI_Send(NULL, 0, MPI_INT, process_id, STATUS_SLEEP, MPI_COMM_WORLD);
			//printf("Master : terminate task, tasks remain : %d\n", task_running);
			printf("1\n");
		}
		else {
			if (row_cnt < DISPLAY_HEIGHT) {
				for (int i = 0; i < block_width; i++) {
					rows[i] = row_cnt++;
				}
				MPI_Send(rows, block_width, MPI_INT, process_id, STATUS_CALC, MPI_COMM_WORLD);
				task_running++;
				//printf("Master : Sending Rows, from %d to %d, tasks remain : %d\n", rows[0], rows[block_width - 1], task_running);
				printf("2\n");
			}
			else {
				MPI_Send(NULL, 0, MPI_INT, process_id, STATUS_SLEEP, MPI_COMM_WORLD);
				//printf("Master : terminate task, tasks remain : %d\n", task_running);
				printf("3\n");
			}
		}
		printf("4\n");
		for (int i = 0; i < block_width; i++) {
			printf("5\n");
			int data_offset = (DISPLAY_HEIGHT + 1) * i;
			int row_num = data[data_offset];
			int offset = row_num * (DISPLAY_HEIGHT + 1);

			result[offset] = data[data_offset];
			for (int col = 0; col < DISPLAY_HEIGHT; col++) {
				result[offset + col + 1] = data[data_offset + col + 1];
			}
		}
	}
	generateBitmapImage();
	printf("Master : result image generated\n");
	free(data);
	free(rows);
}

void generateBitmapImage()
{
	int widthInBytes = DISPLAY_WIDTH * 3;

	unsigned char padding[3] = { 0, 0, 0 };
	int paddingSize = (4 - (widthInBytes) % 4) % 4;

	int stride = (widthInBytes)+paddingSize;

	FILE* imageFile = fopen("output.bmp", "wb");

	unsigned char* fileHeader = createBitmapFileHeader(DISPLAY_HEIGHT, stride);
	fwrite(fileHeader, 1, FILE_HEADER_SIZE, imageFile);

	unsigned char* infoHeader = createBitmapInfoHeader(DISPLAY_HEIGHT, DISPLAY_WIDTH);
	fwrite(infoHeader, 1, INFO_HEADER_SIZE, imageFile);

	for (int i = 0; i < DISPLAY_HEIGHT; i++) {
		for (int j = 0; j < DISPLAY_WIDTH; j++) {
			int index = (DISPLAY_HEIGHT+1) * i + j + 1;
			image[i][j][2] = (unsigned char)(result[index]);
			image[i][j][1] = (unsigned char)(result[index]);
			image[i][j][0] = (unsigned char)(result[index]);
			fprintf(imageFile, "%c", image[i][j][2]);
			fprintf(imageFile, "%c", image[i][j][1]);
			fprintf(imageFile, "%c", image[i][j][0]);
		}
		for (int j = 0; j < paddingSize; j++) {
			fprintf(imageFile, "%c", 0);
		}

	}
	fclose(imageFile);
}

unsigned char* createBitmapFileHeader(int height, int stride)
{
	int fileSize = FILE_HEADER_SIZE + INFO_HEADER_SIZE + (stride * height);

	static unsigned char fileHeader[] = {
		0,0,     /// signature
		0,0,0,0, /// image file size in bytes
		0,0,0,0, /// reserved
		0,0,0,0, /// start of pixel array
	};

	fileHeader[0] = (unsigned char)('B');
	fileHeader[1] = (unsigned char)('M');
	fileHeader[2] = (unsigned char)(fileSize);
	fileHeader[3] = (unsigned char)(fileSize >> 8);
	fileHeader[4] = (unsigned char)(fileSize >> 16);
	fileHeader[5] = (unsigned char)(fileSize >> 24);
	fileHeader[10] = (unsigned char)(FILE_HEADER_SIZE + INFO_HEADER_SIZE);

	return fileHeader;
}

unsigned char* createBitmapInfoHeader(int height, int width)
{
	static unsigned char infoHeader[] = {
		0,0,0,0, /// header size
		0,0,0,0, /// image width
		0,0,0,0, /// image height
		0,0,     /// number of color planes
		0,0,     /// bits per pixel
		0,0,0,0, /// compression
		0,0,0,0, /// image size
		0,0,0,0, /// horizontal resolution
		0,0,0,0, /// vertical resolution
		0,0,0,0, /// colors in color table
		0,0,0,0, /// important color count
	};

	infoHeader[0] = (unsigned char)(INFO_HEADER_SIZE);
	infoHeader[4] = (unsigned char)(width);
	infoHeader[5] = (unsigned char)(width >> 8);
	infoHeader[6] = (unsigned char)(width >> 16);
	infoHeader[7] = (unsigned char)(width >> 24);
	infoHeader[8] = (unsigned char)(height);
	infoHeader[9] = (unsigned char)(height >> 8);
	infoHeader[10] = (unsigned char)(height >> 16);
	infoHeader[11] = (unsigned char)(height >> 24);
	infoHeader[12] = (unsigned char)(1);
	infoHeader[14] = (unsigned char)(3 * 8);

	return infoHeader;
}


int main(int argc, char* argv[])
{
	int np, me;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);

	if (np < 2) {
		printf("Initialize : processor number should be larger than 2\n");
		MPI_Finalize();
		exit(0);
		return 0;
	}

	printf("Initialize : processor number : %d\n", np);
	printf("Initialize : Number of arguments : %d\n", argc);
	printf("Initialize : Selected Task Assignment Type : ");

	isStatic = argc >= 2 ? 1 : 0;
	int block_width = DISPLAY_WIDTH;
	if (isStatic) {
		printf("static\n");
		block_width = DISPLAY_WIDTH / (np - 1);
	}
	else {
		printf("dynamic\n");
		block_width = DISPLAY_WIDTH / N_rectanle;
	}
	


	if (me == 0) { // Master
		printf("Master : Master Process Start\n");
		MasterProcess(np, block_width);
		printf("Master : Process done\n\n");
	}
	else { // Slave
		printf("Slave %d : Slave Process Start\n", me);
		SlaveProcess(me, block_width);
		printf("Slave %d : Process done\n\n", me);
	}
	MPI_Finalize();
	exit(0);
	return 0;
}