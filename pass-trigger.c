#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <predict/predict.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#define NUM_CHARS_IN_TLE 80
#define NUM_TIME_CHARS 80
#define ELEVATION_THRESHOLD_DEGREES 0
#define NUM_CHARS_IN_FILENAME 512

/**
 * Parse a TLE file for a specific satellite number and return as parsed orbital elements.
 * Based on tle_db_from_file() in flyby, in turn based on ReadDataFiles() from Predict.
 *
 * \param tle_file Path to TLE file
 * \param satellite_number Satellite number
 * \param output_satellite_name Output satellite name
 * \return Parsed orbital elements. Will return NULL if something went wrong (file not found or satellite number not found in file)
 **/
predict_orbital_elements_t *orbital_elements_from_file(const char *tle_file, long satellite_number, char **output_satellite_name);

/**
 * Start audio capture.
 *
 * \param filename Filename for writing sound file
 * \return PID of audio capture process
 **/
pid_t start_capture(char *filename);

int main(int argc, char *argv[])
{
	if (argc <= 4) {
		fprintf(stderr, "Usage: %s tle_file satellite_number qth_latitude(N) qth_longitude(E)\n", argv[0]);
		return 1;
	}
	char *tle_filename = argv[1];
	long satellite_number = atol(argv[2]);
	double lat = strtod(argv[3], NULL);
	double lon = strtod(argv[4], NULL);

	//get orbital elements from TLE file
	char *satellite_name;
	predict_orbital_elements_t *orbital_elements = orbital_elements_from_file(tle_filename, satellite_number, &satellite_name);
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

		if (observation.elevation*180.0/M_PI > ELEVATION_THRESHOLD_DEGREES) {
			//construct filename
			char filename[NUM_CHARS_IN_FILENAME] = {0};
			time_t curr_unix_time = time(NULL);
			strftime(time_string, NUM_TIME_CHARS, "%F-%H%M%S", gmtime(&curr_unix_time));
			snprintf(filename, NUM_CHARS_IN_FILENAME, "%s-%s.wav", satellite_name, time_string);

			//start capture
			fprintf(stderr, "Starting capture...\n");
			pid_t capture_pid = start_capture(filename);

			//sleep through the pass
			predict_julian_date_t los_time = predict_next_los(qth, orbital_elements, curr_time);
			sleep((los_time - curr_time)*24*60*60);
			curr_time = predict_to_julian(time(NULL));

			//stop capture
			kill(capture_pid, 9);
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

predict_orbital_elements_t *orbital_elements_from_file(const char *tle_file, long satellite_number, char **output_satellite_name)
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
			*output_satellite_name = strdup(name);
			fclose(fd);
			return temp_elements;
		}
		predict_destroy_orbital_elements(temp_elements);
	}

	fclose(fd);
	return NULL;
}

pid_t start_capture(char *filename)
{
	char cmd[] = "/usr/bin/arecord";
	char *args[] = {"/usr/bin/arecord", "-D", "pulse", "-f", "S16_LE", filename, NULL};
	int retval = 0;

	pid_t ret_pid;
	switch (ret_pid = fork()) {
		case -1:
			fprintf(stderr, "Failed to fork process.");
			break;
		case 0:
			retval = execv(cmd, args);
			break;
		default:
			return ret_pid;
	}
}
