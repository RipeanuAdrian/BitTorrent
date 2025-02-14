#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>  
#include <ctime> 
#include <chrono>
#include <random>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

//tags that i used for MPI MESSAGES 
#define PEER_WANTS_THIS_FILE 1000
#define PEER_FINISH_DOWNLOADING 1001
#define PEER_FINISH_DOWNLOADING_ALL 1002
#define END_OF_WORK 1003
#define MESSAGE_TO_UPLOAD 1004
#define UPDATE_SWARM 1005


typedef struct{
    string file_name;
    vector<string> hashes; //the segemnts
    string role;

}file_entry;  //how the data is contained in clients

typedef struct{ 
     vector <int> seed;
     vector <int> peer;

}swarms; //type of clients roles    

typedef struct{
    string file_name;
    vector<string> segments;
    swarms swarm; // which clients contains segments of this file

}tracker_entry;

typedef struct{
    int rank;
    vector <file_entry> *files;
    vector <string> *whishes; //the files that clients wants to download
    pthread_mutex_t *mutex;

}download_thread_struct;

int search_the_segment(vector<file_entry> files, string needed_segmnet){ //search the client files for a specific segment

    for (long unsigned int i = 0; i < files.size(); i++) {

        for (long unsigned int j = 0;  j < files[i].hashes.size(); j++) {

            if (needed_segmnet == files[i].hashes[j]) {
                return j;
            }
        }
    }
    return -1;
}

void *download_thread_func(void *arg)
{   
    download_thread_struct args_in_download = *(download_thread_struct*) arg; //argumnets used to syncronize teh data with the main thread
    int length_of_swarm_seed = -1;
    int length_of_swarm_peer = -1;
    int lenght_of_required_segments = -1; //how many segments i need to download the file
    MPI_Status status;
    
    for (long unsigned int i = 0; i < args_in_download.whishes->size(); i++) { //download each files that the clients wants

        file_entry new_file;
        new_file.role = "peer";
        new_file.file_name = (*(args_in_download.whishes))[i];

        int size_of_message = (*(args_in_download.whishes))[i].size() + 1;
        char a[size_of_message]; 
        vector <string> required_segments;

        strcpy(a, (*(args_in_download.whishes))[i].c_str());
        MPI_Send(&size_of_message, 1,  MPI_INT, 0, PEER_WANTS_THIS_FILE, MPI_COMM_WORLD);
        MPI_Send(a, size_of_message, MPI_CHAR, 0 , PEER_WANTS_THIS_FILE, MPI_COMM_WORLD); //send the name of file to the tracker
        //and expects to recieve the swarm of the file and the list of the segments i need to download
        
        MPI_Recv(&lenght_of_required_segments, 1, MPI_INT, 0, args_in_download.rank, MPI_COMM_WORLD, &status);

        for (int j = 0; j < lenght_of_required_segments; j++) { //receive from tracker the segments names

                int size_of_segment = -1;
                MPI_Recv(&size_of_segment, 1, MPI_INT, 0, args_in_download.rank, MPI_COMM_WORLD, &status);

                char buffer_segment[size_of_segment];
                MPI_Recv(buffer_segment, size_of_segment, MPI_CHAR, 0, args_in_download.rank, MPI_COMM_WORLD, &status);

                string segment(buffer_segment);
                required_segments.push_back(segment); 

            }

        MPI_Recv(&length_of_swarm_seed, 1, MPI_INT, 0,  args_in_download.rank, MPI_COMM_WORLD, &status);

        //recieve the needed swarm of seeds
        vector <int> use_swarm_seed(length_of_swarm_seed); // the current swarm for seeds that contains needed segments
        MPI_Recv(use_swarm_seed.data(), length_of_swarm_seed, MPI_INT, 0, args_in_download.rank, MPI_COMM_WORLD, &status);

        MPI_Recv(&length_of_swarm_peer, 1, MPI_INT, 0,  args_in_download.rank, MPI_COMM_WORLD, &status);
        vector <int> use_swarm_peer(length_of_swarm_peer); // the current swarm for peers that I need to complete a file

        if (length_of_swarm_peer > 0) { //if any peers exists

            MPI_Recv(use_swarm_peer.data(), length_of_swarm_peer, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
        }

        int new_position_of_file = (*(args_in_download.files)).size(); //the position of the file in the files list of the client
        (*(args_in_download.files)).push_back(new_file);


        int num_of_successful_seg_downloads = 0; 
        int min_peer = 0; //the first possible peer element 
        int max_peer = use_swarm_peer.size() - 1; //the last possible peer element 
        int min_seed = 0; //the first possible seed element
        int max_seed = use_swarm_seed.size() - 1; //the last possible seed element 

        //random number selector using hardware
        random_device rand_device;
        mt19937 generator(rand_device());

        //search for the each needed segment in the swarm and download it
        for (long unsigned int  j = 0; j  < required_segments.size(); j++){
                
            bool downloaded = false;
            retry: //if the segement isn't found, which is improbabile try again (safe coding)
            
            if (num_of_successful_seg_downloads % 10 == 0){ // at 10 succesful downloads of segments update the swarm 
                
                MPI_Send(&size_of_message, 1,  MPI_INT, 0, UPDATE_SWARM, MPI_COMM_WORLD);
                MPI_Send((*(args_in_download.whishes))[i].c_str(), size_of_message, MPI_CHAR, 0 , UPDATE_SWARM, MPI_COMM_WORLD);

                length_of_swarm_seed = -1;
                MPI_Recv(&length_of_swarm_seed, 1, MPI_INT, 0,  args_in_download.rank, MPI_COMM_WORLD, &status);
                use_swarm_seed.clear();  //clear the seed swarm
                use_swarm_seed.resize(length_of_swarm_seed);
                MPI_Recv(use_swarm_seed.data(), length_of_swarm_seed, MPI_INT, 0, args_in_download.rank, MPI_COMM_WORLD, &status);

                length_of_swarm_peer = -1;
                MPI_Recv(&length_of_swarm_peer, 1, MPI_INT, 0,  args_in_download.rank, MPI_COMM_WORLD, &status);
                use_swarm_peer.clear();  //clear the peer swarm
                use_swarm_peer.resize(length_of_swarm_peer);
        
                if (length_of_swarm_peer > 0) {

                MPI_Recv(use_swarm_peer.data(), length_of_swarm_peer, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
                }
                // the intervals used for the random selection of elemnetnts in swarm

                int min_peer = 0; //the first possible peer element 
                int max_peer = use_swarm_peer.size() - 1; //the last possible peer element 
                int min_seed = 0; //the first possible seed element 
                int max_seed = use_swarm_seed.size() - 1; //the last possible seed element 

                num_of_successful_seg_downloads = 0;
            }

            if (num_of_successful_seg_downloads %10 < 5){ //first 5 dowload atempts is from peers

                if ( max_peer >= 0){ //if I have any peers for this file

                    //chose randomly the peer index from swarm of peers
                    uniform_int_distribution<> interval(min_peer, max_peer);
                    int peer_index = interval(generator);
                    int len_current_segment = required_segments[j].size() + 1;
                    char confirmation[3]; //if the segment is found and downloaded 

                    //send download request to the upload tread of the specified client peer
                    MPI_Send(&len_current_segment, 1, MPI_INT, use_swarm_peer[peer_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD); 
                    MPI_Send(required_segments[j].c_str(), len_current_segment, MPI_CHAR, use_swarm_peer[peer_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
                    MPI_Recv(confirmation, 3, MPI_CHAR, use_swarm_peer[peer_index], args_in_download.rank, MPI_COMM_WORLD, &status);

                    //if the file is found and downloaded
                    if (strcmp(confirmation, "OK") == 0) {

                        pthread_mutex_lock(args_in_download.mutex);
                        (*(args_in_download.files))[new_position_of_file].hashes.push_back(required_segments[j]);
                        pthread_mutex_unlock(args_in_download.mutex);

                        downloaded = true;
                        num_of_successful_seg_downloads++; //a segment downloaded with succes
                    }

                    if (downloaded == false) { //if the segment isn't found try again in the seeds list
                        goto no_peer;
                    }
                }
                else { no_peer:  //if the segment isn't found and downloaded in the peers swarm search for it in the seed swarm

                    //chose randomly the seed index from swarm of peers
                    uniform_int_distribution<> interval(min_seed, max_seed);
                    int seed_index = interval(generator);
                    int len_current_segment = required_segments[j].size() + 1;
                    char confirmation[4]; //if the segment is found and downloaded

                    //send download request to the upload tread of the specified client seed
                    MPI_Send(&len_current_segment, 1, MPI_INT, use_swarm_seed[seed_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
                    MPI_Send(required_segments[j].c_str(), len_current_segment, MPI_CHAR, use_swarm_seed[seed_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
                    MPI_Recv(confirmation, 3, MPI_CHAR, use_swarm_seed[seed_index], args_in_download.rank, MPI_COMM_WORLD, &status);

                    //if the file is found and downloaded
                    if (strcmp(confirmation, "OK") == 0) {

                        pthread_mutex_lock(args_in_download.mutex);
                        (*(args_in_download.files))[new_position_of_file].hashes.push_back(required_segments[j]);
                        pthread_mutex_unlock(args_in_download.mutex);

                        downloaded = true;
                        num_of_successful_seg_downloads++;
                    }
                }
                if (downloaded == false) { //if the segment isn't found try again
                        goto retry;
                }

            }
            else if (num_of_successful_seg_downloads % 10 >= 5) { //last 5 dowload atempts is for seed clients
                
                    //chose randomly the seed index from swarm of peers
                    uniform_int_distribution<> interval(min_seed, max_seed);
                    int seed_index = interval(generator);
                    int len_current_segment = required_segments[j].size() + 1;
                    char confirmation[4];

                    //send download request to the upload tread of the specified client seed
                    MPI_Send(&len_current_segment, 1, MPI_INT, use_swarm_seed[seed_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
                    MPI_Send(required_segments[j].c_str(), len_current_segment, MPI_CHAR, use_swarm_seed[seed_index], MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
                    MPI_Recv(confirmation, 3, MPI_CHAR, use_swarm_seed[seed_index], args_in_download.rank, MPI_COMM_WORLD, &status);

                    //if the file is found and downloaded
                    if (strcmp(confirmation, "OK") == 0) {

                        pthread_mutex_lock(args_in_download.mutex);
                        (*(args_in_download.files))[new_position_of_file].hashes.push_back(required_segments[j]);
                        pthread_mutex_unlock(args_in_download.mutex);

                        downloaded = true;
                        num_of_successful_seg_downloads++;
                    }

                    if (downloaded == false) { //if the segment isn't found try again in the seeds list
                        goto retry;
                    }
                }
        }
        //the file now is complete downloaded with all the segments

        (*(args_in_download.files))[new_position_of_file].role = "seed"; //now is a seed no more peer

        string name_file_to_write = "client" + to_string(args_in_download.rank) + "_" + a;
        ofstream file_to_write(name_file_to_write);

        for (long unsigned int it_write = 0 ; it_write < required_segments.size(); it_write++){ //write the the download file and segemnts in a file
            
            file_to_write << (*(args_in_download.files))[new_position_of_file].hashes[it_write]<<endl;
        }
        file_to_write.close();

        //signal the tracker that this file is complete downloaded
        MPI_Send(&size_of_message, 1,  MPI_INT, 0, PEER_FINISH_DOWNLOADING, MPI_COMM_WORLD);
        MPI_Send(a, size_of_message, MPI_CHAR, 0 , PEER_FINISH_DOWNLOADING, MPI_COMM_WORLD);
    }

    //no more files to download (signal that to tracker)

    char finish_downloading_all_file[2] = "E";
    int size_of_finsh_all_message = 2;
    MPI_Send(&size_of_finsh_all_message,  1, MPI_INT, 0, PEER_FINISH_DOWNLOADING_ALL, MPI_COMM_WORLD);
    MPI_Send(&finish_downloading_all_file,  size_of_finsh_all_message, MPI_CHAR, 0, PEER_FINISH_DOWNLOADING_ALL, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{   
    download_thread_struct args_in_upload = *(download_thread_struct*) arg; //arguments for upload thread
    int message_len = -1;
    MPI_Status status;
    MPI_Status status2;
    char return_message[3];

    while(1) { //until the tracker signal the upload thread to stop the work
        
        //recive message
        MPI_Recv(&message_len, 1, MPI_INT, MPI_ANY_SOURCE, MESSAGE_TO_UPLOAD, MPI_COMM_WORLD, &status);
        char message[message_len];
        MPI_Recv(&message, message_len, MPI_CHAR, status.MPI_SOURCE, MESSAGE_TO_UPLOAD, MPI_COMM_WORLD, &status2);
        string message_string (message);

        if (message_string == "END") { //the tracker signal to stop the update thread

            return NULL;
        }

        //search for the segment in all the files of this client
        pthread_mutex_lock(args_in_upload.mutex);
        int index_of_segmnet = search_the_segment(*args_in_upload.files,  message_string);
        pthread_mutex_unlock(args_in_upload.mutex);

        if (index_of_segmnet == -1) {
            
            strcpy(return_message, "NO"); //segmnet wasn't found
        }

        else{
            strcpy(return_message, "OK"); //segmnet was found
        }

        MPI_Send(return_message, sizeof(return_message), MPI_CHAR, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD); //send the message
    }
    return NULL;
}

int already_in_tracker(vector<tracker_entry> files, string filename){ //search for a file in the files list

    for (long unsigned int i = 0; i < files.size(); i++) {

        if (files[i].file_name == filename) {
            return i;
        }
    }
    return -1;
}

void tracker(int numtasks, int rank) {

    vector<tracker_entry> files;
    int still_working[numtasks]; //this client wants to download files
    int running = numtasks - 1; //if the tracker has work to be done

    //if the tasks have something to do
    for (int i = 0; i < numtasks; i++) {

        still_working[i] = 1;
    }

    //receive all the data from the clients to initialize the tracker
    for (int i = 1; i < numtasks; i++) {

        MPI_Status status;
        int num_files_in_seed = -1;

        MPI_Recv(&num_files_in_seed, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);

        //for each client receive all thier files and segments name
        for (int z = 0; z < num_files_in_seed; z++){
            
            int size_of_file_name = 0;
            int segments_num = 0; //number of segments

            //receieve the file name
            MPI_Recv(&size_of_file_name, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);
            char buffer[size_of_file_name + 1]; //receive the file name

            MPI_Recv(buffer, size_of_file_name, MPI_CHAR, i, i, MPI_COMM_WORLD, &status);
            buffer[size_of_file_name] = '\0';
            string file_name (buffer);

            int position_of_existing_file = already_in_tracker(files, file_name); //search if the file has been already added

            if  (position_of_existing_file == -1){ //if the file wasn't already added

                char message[11] = "NEW_FILEIN"; //the file isn't already in tracker
                tracker_entry new_file; //new file entry in tracker

                new_file.file_name = file_name;
                new_file.swarm.seed.push_back(i); //add teh client to the swarm

                //signal the client that this is a new file in tracker and that the seed should send also the segments name
                MPI_Send(message, strlen(message) + 1, MPI_CHAR, i, i, MPI_COMM_WORLD);
                MPI_Recv(&segments_num, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);

                for (int j = 0; j < segments_num; j++) { //receive from the seed client the segments names
                
                    int size_of_segment = -1;

                    MPI_Recv(&size_of_segment, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);
                    char buffer_segment[size_of_segment + 1];
                    MPI_Recv(buffer_segment, size_of_segment, MPI_CHAR, i, i, MPI_COMM_WORLD, &status);
                    buffer_segment[size_of_segment] = '\0';

                    string segment(buffer_segment);
                    new_file.segments.push_back(segment); //new segment name
                }

                files.push_back(new_file); //add teh file to files list of names
            }

            else {
                char message[11] = "ALREADY_IN"; // the file name is already in the tracker
                MPI_Send(message, strlen(message), MPI_CHAR, i, i, MPI_COMM_WORLD);

                files[position_of_existing_file].swarm.seed.push_back(i); //add the clients to the swarm of seeds
            }

        }
        //end of initialization for this client
    }

    //signal all the others clients that the tracker hass been initialized and the clients can start downloading the files from each other
    for (int i = 1; i < numtasks; i++) {

        char message[4] = "ACK";
        MPI_Send(message, strlen(message) + 1, MPI_CHAR, i, i, MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD); //used to make sure that all the clients start aproximatly at the same time

    int length_message = -1;

    while(running != 0) //run until there is no more clients that have something to do
    {
        MPI_Status status;
        MPI_Recv(&length_message, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        char buffer_segment[length_message];
        MPI_Status status2;
        MPI_Recv(buffer_segment, length_message, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status2); //the message received

        if (status2.MPI_TAG == UPDATE_SWARM) { //update the swarm of the client

            int index_of_desired_file = already_in_tracker(files, buffer_segment); //the file for wich i want to update the swarm
            int length_of_swarm_seed = files[index_of_desired_file].swarm.seed.size();
            int length_of_swarm_peer = files[index_of_desired_file].swarm.peer.size();

            MPI_Send(&length_of_swarm_seed, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);
            MPI_Send(files[index_of_desired_file].swarm.seed.data(), length_of_swarm_seed, MPI_INT, status.MPI_SOURCE,  status.MPI_SOURCE, MPI_COMM_WORLD);
            MPI_Send(&length_of_swarm_peer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);

            if (length_of_swarm_peer > 0) { //if any peer exists

                MPI_Send(files[index_of_desired_file].swarm.peer.data(), length_of_swarm_peer, MPI_INT, status.MPI_SOURCE,  status.MPI_SOURCE, MPI_COMM_WORLD);
            }

        }
        else if(status2.MPI_TAG == PEER_WANTS_THIS_FILE){   //send the swarm of a file to a client that requested it to download

            int index_of_desired_file = already_in_tracker(files, buffer_segment);
            int length_of_swarm_seed = files[index_of_desired_file].swarm.seed.size();
            int length_of_swarm_peer = files[index_of_desired_file].swarm.peer.size();
            int segments_size = files[index_of_desired_file].segments.size();

            MPI_Send(&segments_size, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);

            for (int i = 0; i < segments_size; i++) {   //send the segments names

                int lenght_of_current_segment = files[index_of_desired_file].segments[i].size() + 1;

                MPI_Send(&lenght_of_current_segment, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);
                MPI_Send(files[index_of_desired_file].segments[i].c_str(), lenght_of_current_segment, MPI_CHAR, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);
            }

            MPI_Send(&length_of_swarm_seed, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);
            MPI_Send(files[index_of_desired_file].swarm.seed.data(), length_of_swarm_seed, MPI_INT, status.MPI_SOURCE,  status.MPI_SOURCE, MPI_COMM_WORLD);
            MPI_Send(&length_of_swarm_peer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);

            if (length_of_swarm_peer > 0) {     //if any peer exists

                MPI_Send(files[index_of_desired_file].swarm.peer.data(), length_of_swarm_peer, MPI_INT, status.MPI_SOURCE,  status.MPI_SOURCE, MPI_COMM_WORLD);
            }

            files[index_of_desired_file].swarm.peer.push_back(status.MPI_SOURCE); //add the client to the swarm list of peers
        }

        else if (status2.MPI_TAG == PEER_FINISH_DOWNLOADING) {      //the client finished downloading the file that wanted

            int index_of_desired_file = already_in_tracker(files, buffer_segment);
            int client_to_be_deleted = status.MPI_SOURCE;

            for (long unsigned int i = 0;  i < files[index_of_desired_file].swarm.peer.size(); i++){ //search for the peer entry in the swarm and delete it

                if (files[index_of_desired_file].swarm.peer[i] == status.MPI_SOURCE){
                    
                    files[index_of_desired_file].swarm.peer.erase(files[index_of_desired_file].swarm.peer.begin() + i);
                    break;
                }
            }

            //move the client from peer to seed swarm
            files[index_of_desired_file].swarm.seed.push_back(status.MPI_SOURCE);
        }
        //the download tread can shut down
        else if (status2.MPI_TAG == PEER_FINISH_DOWNLOADING_ALL) { //the client finished downloading all the files that wanted

            running --;
        }
    }

    //for the upload threads
    for (int i = 1; i < numtasks; i++) { //signal all the the clients that all the files have finished downloading and now can shut down


        char message[4] = "END";
        int len_message = strlen(message) + 1;

        MPI_Send(&len_message, 1, MPI_INT, i, MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
        MPI_Send(message, len_message, MPI_CHAR, i, MESSAGE_TO_UPLOAD, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank) { 

    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;
    int n;
    int num_disered_files;
    pthread_mutex_t mutex;
    vector<file_entry> files; //list of the filles that the client possesses
    string filename = "in" + to_string(rank) + ".txt";
    vector <string> files_that_client_wants; //files that client wants to download
    
    //open the file
    ifstream file(filename); //open the input file

    if (!file.is_open()) {
        return;
    }

    file >> n; //number of files to be read from the input file
    MPI_Send(&n, 1, MPI_INT, 0, rank, MPI_COMM_WORLD);

    for (int i = 0; i < n; i++){ //read from the input file

        MPI_Status status;
        int new_file_in_tracker = 0; //if the file is new to the tracker

        string file_name; 
        file >> file_name;

        int segments_num;
        file >> segments_num;

        file_entry current_file;
        current_file.role = "seed";
        current_file.file_name = file_name;
        int size_of_file_name = file_name.size();
        
        //send the file name to the tracker
        MPI_Send(&size_of_file_name, 1, MPI_INT, 0, rank, MPI_COMM_WORLD);
        MPI_Send(file_name.c_str(), file_name.size(), MPI_CHAR, 0, rank, MPI_COMM_WORLD);

        char message[12]; //if the files is already or isn't in tracker
        MPI_Recv(message, 11, MPI_CHAR, 0, rank, MPI_COMM_WORLD, &status);

        if (strcmp(message, "ALREADY_IN") != 0){ // if the tracker doesn't know about his file

            MPI_Send(&segments_num, 1, MPI_INT, 0, rank, MPI_COMM_WORLD);
            new_file_in_tracker = 1; //the file is new to the tracker
        }
        
        for (int j = 0; j < segments_num; j++){

            string hash;
            int size_of_segment;

            file >> hash;
            size_of_segment = hash.size();
            current_file.hashes.push_back(hash);

            if (new_file_in_tracker == 1) { //no need to send segments names if the tracker already has them

                MPI_Send(&size_of_segment, 1, MPI_INT, 0, rank, MPI_COMM_WORLD);
                MPI_Send(hash.c_str(), hash.size(), MPI_CHAR, 0, rank, MPI_COMM_WORLD);
            }
        }
        files.push_back(current_file);
    }

    file >> num_disered_files; //number of files that this client wants to download

    for (int i = 0 ; i < num_disered_files; i++) { //files that client wants to download

        string file_name;
        file >> file_name;

        files_that_client_wants.push_back(file_name);
    }
    file.close(); //close the input file

    MPI_Status status2;
    char message[5];
    MPI_Recv(message, 4, MPI_CHAR, 0, rank, MPI_COMM_WORLD, &status2); //wait to recieve the signal from the tracker to start the work
    MPI_Barrier(MPI_COMM_WORLD);

    //the data that I want to syncronize
    pthread_mutex_init(&mutex, NULL);
    download_thread_struct args_for_threads;
    args_for_threads.rank = rank;
    args_for_threads.files = &files;
    args_for_threads.whishes = &files_that_client_wants;
    args_for_threads.mutex = &mutex;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &args_for_threads);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &args_for_threads);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }    
}
 
int main (int argc, char *argv[]) {

    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
