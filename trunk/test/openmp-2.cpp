#include <iostream>
using namespace std;

#include <omp.h>

void MetaTestOptimize();
void ScoreMetaCallback();
void ScoreCallback(unsigned int m);


const unsigned int size2 = 10000;
const unsigned int size3 = 10000;
const unsigned int size4 = 1000;


int **memarray;

double scratch[4] = {0, 0, 0, 0};


int main(int argc, char *argv[])
{
	omp_set_num_threads(4);
	cout << "Number of OpenMP threads: " << omp_get_max_threads() << endl;

	MetaTestOptimize();

	cout << "scratch[0]: " << scratch[0] << endl;
	cout << "scratch[1]: " << scratch[1] << endl;
	cout << "scratch[2]: " << scratch[2] << endl;
	cout << "scratch[3]: " << scratch[3] << endl;


  return 0;
}



void MetaTestOptimize()
{
	omp_set_num_threads(1);

	unsigned int j = 0;
#pragma omp parallel for \
   private(j) \
   schedule(static)
	for (j = 0; j < size2; j++) {
		cout << "j: " << j << endl;
		ScoreMetaCallback();
	}

}


void ScoreMetaCallback()
{
	omp_set_num_threads(4);

	// DMTCP hangs with this uncommented:
	#pragma omp critical(ScoreMetaCallback)
	{
		memarray = new int*[size3];
		unsigned int m = 0;

		#pragma omp parallel for \
		   private(m) \
		   schedule(static)
			for (m = 0; m < size3; m++) {
				//cout << "add m: " << m << endl;
				memarray[m] = new int[size4];
			}

		#pragma omp parallel for \
		   private(m) \
		   schedule(static)
			for (m = 0; m < size3; m++) {
				cout << "m: " << m << endl;
				ScoreCallback(m);
			}


		#pragma omp parallel for \
		   private(m) \
		   schedule(static)
			for (m = 0; m < size3; m++) {
				//cout << "del m: " << m << endl;
				delete[] memarray[m];
			}

			delete[] memarray;
	}
}


void ScoreCallback(unsigned int m)
{
	int threadNum = omp_get_thread_num();
	scratch[threadNum] += m * (threadNum - 2);   // meaningless calculation
}

