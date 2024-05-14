/*  Name       :  server_udp_broadcast.c
 Author     :  Luis A. Rivera
 Description:  Simple server (broadcast)
         ECE4220/7220    */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>
#include <mosquitto.h>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <strings.h>
#include <stdbool.h>

#define CHAR_DEV "/dev/Lab6" // "/dev/YourDevName"

// Important variables
bool MASTER = false;
bool VOTED = false;
int cdev_id;
int random_number;
int their_highest_random_number = -1;
bool master_tie_already_set = false;
struct mosquitto *mosq;

// Use Ramy IP as broker/server
// Use mosquitto instead of UDP

// mosquitto_publish() to send a message

#define MSG_SIZE 40 // Standardized message size

void error(const char *msg)
{
  perror(msg);
  exit(0);
}

// Called when broker sends a CONNACK msg in response to connection
void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
  // Subscribe to topic
  mosquitto_subscribe(mosq, NULL, "Election", 0);
}

// Called when msg received from broker
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
  // Note: msg struct has "topic" and "payload" fields

  // Get IP dynamically
  char my_ip[15];
  char *command = "ifconfig | grep 'inet ' | grep -v '127.0.0.1' | awk '{print $2}' | head -n 1";
  FILE *pipe = popen(command, "r");
  if (!pipe)
  {
    error("Error: Failed to run command.\n");
  }
  // Read the output of the command
  if (fgets(my_ip, sizeof(my_ip), pipe) != NULL)
  {
    // Remove the newline character from the end
    my_ip[strcspn(my_ip, "\n")] = 0;
  }
  else
  {
    printf("Failed to retrieve IP address.\n");
  }
  pclose(pipe);

  bool case1 = false;
  bool case2 = false;
  char buffer[MSG_SIZE];
  strcpy(buffer, msg->payload);

  printf("Got message: %s.\n", buffer);

  if (strcmp(buffer, "VOTE") == 0)
  {
    // Reset election
    their_highest_random_number = -1;
    master_tie_already_set = false;

    case1 = true;
  }
  else if (strcmp(buffer, "WHOIS") == 0)
  {
    case2 = true;
  }

  if (case1) // Received vote
  {
    // Convert random number to string
    srand(time(NULL));
    random_number = rand() % 10;
    char random_number_string[3];
    sprintf(random_number_string, "%d", random_number);
    char message[21];
    snprintf(message, 21, "# %s %s\n", my_ip, random_number_string);

    printf("Message to send: %s\n", message);

    mosquitto_publish(mosq, NULL, "Election", sizeof(message), message, 0, true);

    VOTED = true;
  }
  else if (buffer[0] == '#' && VOTED) // Received a vote and I voted and it's not from same IP
  {
    // Get IP and random number
    char their_message_without_hashtag[17];
    strcpy(their_message_without_hashtag, buffer + 2);
    char *token = strtok(their_message_without_hashtag, " ");
    char their_ip[15];
    strcpy(their_ip, token);

    // Make sure it isn't same IP
    // printf("my_ip %s\n", my_ip);
    // printf("their_ip %s\n", their_ip);
    if (strcmp(my_ip, their_ip) != 0)
    {
      token = strtok(NULL, " ");
      int their_random_number = atoi(token);

      // Get highest
      if (their_random_number > their_highest_random_number)
      {
        their_highest_random_number = their_random_number;
      }

      printf("Received other vote and I voted.\n");

      printf("my random: %d, their highest random: %d\n", random_number, their_highest_random_number);
      // Check if I am the master or not
      if (random_number > their_highest_random_number)
      {
        MASTER = true;
      }
      else if ((random_number == their_highest_random_number) && !master_tie_already_set) // TIE
      {
        master_tie_already_set = true;
        char new_string_holder[3];
        strncpy(new_string_holder, my_ip + strlen(my_ip) - 2, 2);
        new_string_holder[2] = '\0';
        int last_two_my_ip = atoi(new_string_holder);
        strncpy(new_string_holder, their_ip + strlen(my_ip) - 2, 2);
        new_string_holder[2] = '\0';
        int last_two_their_ip = atoi(new_string_holder);
        // printf("my_ip_last: %d\n", last_two_my_ip);
        // printf("their_ip_last: %d\n", last_two_their_ip);
        if (last_two_my_ip >= last_two_their_ip)
        {
          MASTER = true;
        }
        else
        {
          MASTER = false;
        }
      }
      else
      {
        MASTER = false;
      }
    }
  }
  else if (case2 && MASTER) // Received WHOIS and I'm master
  {
    char message[36];
    snprintf(message, 36, "Logan is master on %s\n", my_ip);
    mosquitto_publish(mosq, NULL, "Election", sizeof(message), message, 0, true);
  }
  else if (buffer[0] == '@' && !MASTER)
  {
    int dummy;
    // Get note
    char message[2];
    message[0] = buffer[1];
    message[1] = '\0';

    // Open the Character Device for writing
    if ((cdev_id = open(CHAR_DEV, O_RDWR)) == -1)
    {
      printf("Cannot open device %s\n", CHAR_DEV);
      exit(1);
    }

    // Write msg
    dummy = write(cdev_id, message, sizeof(message));
    if (dummy != sizeof(message))
    {
      printf("Write failed, leaving...\n");
      exit(1);
    }

    close(cdev_id); // close the device.
  }
}

// Used to read music notes being played!
void kernel_message_thread()
{
  char kmsg[MSG_SIZE];
  char prevmsg[MSG_SIZE];

  while (1)
  {
    // printf("About to read.\n");
    if (read(cdev_id, kmsg, sizeof(kmsg)) < 0)
    {
      printf("Read failed.\n");
      close(cdev_id);
      return;
    }
    // printf("Read: %s\n", kmsg);
    // if ((kmsg[0] == 'A' || kmsg[0] == 'B' || kmsg[0] == 'C' || kmsg[0] == 'D' || kmsg[0] == 'E') && MASTER && strcmp(kmsg, prevmsg) != 0)
    if ((kmsg[0] == 'A' || kmsg[0] == 'B' || kmsg[0] == 'C' || kmsg[0] == 'D' || kmsg[0] == 'E') && MASTER)
    {
      printf("Sending msg: %s\n", kmsg);
      mosquitto_publish(mosq, NULL, "Election", sizeof(kmsg), kmsg, 0, true);
      strcpy(prevmsg, kmsg);
    }
  }
}

int main(int argc, char *argv[])
{
  mosquitto_lib_init();

  int rc, id = 1;

  // Create new mosquitto client instance
  mosq = mosquitto_new("subscribe-TA", true, &id);

  // Set callbacks
  mosquitto_connect_callback_set(mosq, on_connect); // Called when broker sends a CONNACK msg in response to connection
  mosquitto_message_callback_set(mosq, on_message); // Called when msg received from broker

  // Set broker host IP
  char *host = "127.0.0.1"; // Change this IP to broker host IP

  // Open the Character Device for writing
  if ((cdev_id = open(CHAR_DEV, O_RDWR)) == -1)
  {
    printf("Cannot open device %s\n", CHAR_DEV);
    exit(1);
  }

  // Connect to MQTT broker
  rc = mosquitto_connect(mosq, host, 1883, 10);
  if (rc)
  {
    printf("Couldn't connect to Broker with return code %d\n");
    return -1;
  }

  // Create thread for reading messages from kernel
  pthread_t kmt;
  if (pthread_create(&kmt, NULL, (void *)&kernel_message_thread, NULL) != 0)
  {
    printf("Create thread failed!\n");
    exit(1);
  }
  else
  {
    printf("Created msg thread.\n");
  }

  // Start new thread to process network traffic
  mosquitto_loop_start(mosq);

  // Join thread
  if (pthread_join(kmt, NULL) != 0)
  {
    printf("Error with joining thread!\n");
  }

  // Copy code to raspberry pi
  // SSH into different terminal on pi, then compile and run

  return 0;
}
