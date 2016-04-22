#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <predict/predict.h>
#include <unistd.h>
#include <string.h>

#define NUM_CHARS_IN_TLE 80

/**
 * Parse a TLE file for a specific satellite number and return as parsed orbital elements.
 * Based on tle_db_from_file() in flyby, in turn based on ReadDataFiles() from Predict.
 *
 * \param tle_file Path to TLE file
 * \param satellite_number Satellite number
 * \return Parsed orbital elements. Will return NULL if something went wrong (file not found or satellite number not found in file)
 **/
predict_orbital_elements_t *orbital_elements_from_file(const char *tle_file, long satellite_number)
{
	FILE *fd = fopen(tle_file,"r");
	if (fd == NULL) {
		return NULL;
	}

	while (feof(fd) == 0) {
		char name[NUM_CHARS_IN_TLE] = {0};
		char line1[NUM_CHARS_IN_TLE] = {0};
		char line2[NUM_CHARS_IN_TLE] = {0};

		//read element set
		fgets(name,75,fd);
		fgets(line1,75,fd);
		fgets(line2,75,fd);

		//trim name
		int y=strlen(name);
		while (name[y]==32 || name[y]==0 || name[y]==10 || name[y]==13 || y==0) {
			name[y]=0;
			y--;
		}

		//parse element set
		char *tle[2] = {line1, line2};
		predict_orbital_elements_t *temp_elements = predict_parse_tle(tle);
		if (temp_elements->satellite_number == satellite_number) {
			fprintf(stderr, "Satellite %s (%ld) found.\n", name, satellite_number);
			fclose(fd);
			return temp_elements;
		}
		predict_destroy_orbital_elements(temp_elements);
	}

	fclose(fd);
	return NULL;
}

#define NUM_TIME_CHARS 80
int main(int argc, char *argv[])
{
	if (argc <= 4) {
		fprintf(stderr, "Usage: %s tle_file satellite_number qth_latitude qth_longitude\n", argv[0]);
		return 1;
	}
	char *tle_filename = argv[1];
	long satellite_number = atol(argv[2]);
	double lat = strtod(argv[3], NULL);
	double lon = strtod(argv[4], NULL);

	//get orbital elements from TLE file
	predict_orbital_elements_t *orbital_elements = orbital_elements_from_file(tle_filename, satellite_number);
	if (orbital_elements == NULL) {
		fprintf(stderr, "Specified TLE not found.\n");
		return 1;
	}

	//construct observer
	predict_observer_t *qth = predict_create_observer("", lat*M_PI/180.0, lon*M_PI/180.0, 0);

	char time_string[NUM_TIME_CHARS];
	while (true) {
		//get current satellite state
		struct predict_orbit orbit;
		struct predict_observation observation;
		predict_julian_date_t curr_time = predict_to_julian(time(NULL));
		predict_orbit(orbital_elements, &orbit, curr_time);
		predict_observe_orbit(qth, &orbit, &observation);

		//start capture when above the horizon
		if (observation.elevation > 0) {
			fprintf(stderr, "Capture trigger\n");
			predict_julian_date_t los_time = predict_next_los(qth, orbital_elements, curr_time);
			fprintf(stderr, "Sleep rest of the pass\n");
			sleep((los_time - curr_time)*24*60*60);
			fprintf(stderr, "Wake up, stop capture\n");
			curr_time = predict_to_julian(time(NULL));
		}

		//check time until next pass
		predict_julian_date_t aos_time = predict_next_aos(qth, orbital_elements, curr_time);
		double seconds_until_aos = (aos_time - curr_time)*24*60*60;
		if (seconds_until_aos > 60) {
			time_t aos_unix_time = predict_from_julian(aos_time);
			strftime(time_string, NUM_TIME_CHARS, "%H:%M:%S", gmtime(&aos_unix_time));

			fprintf(stderr, "Sleeping for %f hours until next AOS (%s).\n", seconds_until_aos/(60.0*60.0), time_string);
			sleep(seconds_until_aos);
		}

	}
}
