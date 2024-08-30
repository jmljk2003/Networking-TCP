#define _POSIX_C_SOURCE 200112L
#define PORT "143"
#define DEFAULT_FOLDER "INBOX"
#define MAX_TAG_SIZE 4
#define MAX_BUFFER_SIZE 256
#define MAX_SIZE_STRING 10
#define MAX_LIST_SIZE 1024
#define EXIT_CODE_3 3
#define EXIT_CODE_4 4

#include <ctype.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>



char *retrieve(int sockfd, char *username, char *password, char *folder,
               int messageNum);
int generate_tag();
int getMessageNum(int sockfd, char *folder);
char *parse(int sockfd, int messageNum);
void mime(char *raw_email);
void list(int sockfd, char *username, char *password, char *folder);
char *removeNewlines(const char *str);
void printTo(char *input);
void printFrom(char *input);
void printDate(char *input);
void printSubject(char *input);
int main(int argc, char **argv) {
    int sockfd, s;
    int opt;
    struct addrinfo hints, *servinfo, *rp;

    char *server_name = NULL;
    char *port = PORT;
    char *username = NULL;
    char *password = NULL;
    char *folder = NULL;
    int messageNum = -1;
    char *command = NULL;

    // Loop through arguments using getopt
    while ((opt = getopt(argc, argv, "u:p:f:n:t:")) != -1) {

        switch (opt) {

        case 'u':
            username = optarg;
            break;
        case 'p':
            password = optarg;
            break;
        case 'f':

            folder = optarg;

            break;
        case 'n':
            messageNum = atoi(optarg);
            break;
        case 't':
            command = optarg;
            break;
        default:
            fprintf(stderr,
                    "Usage: %s -u <username> -p <password> [-f <folder>] [-n "
                    "<messageNum>] -t <command> <server_name>\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Handle leftover non-option arguments
    if (optind < argc) {
        command = argv[optind++];
        if (optind < argc) {
            server_name = argv[optind++];
        }
    } else {
        fprintf(stderr, "Error: Missing positional arguments\n");
        exit(EXIT_FAILURE);
    }

    // Validate the command
    if (command == NULL ||
        (strcmp(command, "retrieve") != 0 && strcmp(command, "parse") != 0 &&
         strcmp(command, "mime") != 0 && strcmp(command, "list") != 0)) {
        fprintf(stderr, "Error: Invalid command\n");
        exit(EXIT_FAILURE);
    }

    // Create address hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Get addrinfo of server
    s = getaddrinfo(server_name, port, &hints, &servinfo);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    // Connect to first valid result
    for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            perror("socket");
            continue;
        }

        // Connected successfully to server
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break; // success
        }
        close(sockfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Login failure\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(servinfo);

    // Read from inbox folder if folder was not specified
    if (folder == NULL) {
        folder = DEFAULT_FOLDER;
    }

    char *output = NULL; // Use this for the output of retrieve

    if (strcmp(command, "retrieve") == 0) {
        output = retrieve(sockfd, username, password, folder, messageNum);
        if (output != NULL) {
            printf("%s\n", output);
            free(output);
        } else {
            printf("Error: Failed to retrieve output\n");
        }
    } else if (strcmp(command, "parse") == 0) {
        output = retrieve(sockfd, username, password, folder, messageNum);
        output = parse(sockfd, messageNum);
        // fprintf(output_file, "%s\n", removeNewlines(output));
    } else if (strcmp(command, "mime") == 0) {
        output = retrieve(sockfd, username, password, folder, messageNum);
        mime(output);
    } else if (strcmp(command, "list") == 0) {
        list(sockfd, username, password, folder);
    }

    close(sockfd);
    return 0;
}

/*This function strives to retrieve the full raw contents of the email found in a particular
folder, filtering out unnecessary output and checks if an email is found.*/
char *retrieve(int sockfd, char *username, char *password, char *folder,
               int messageNum) {
    char buffer[MAX_BUFFER_SIZE];
    char recvline[MAX_BUFFER_SIZE];
    char *output = NULL;
    char *prefix = NULL;
    int tag_number, n;
    char tag[MAX_TAG_SIZE];
    int foundOpeningBracket = 0;
    int foundClosingBracket = 0;
    int char_count = 0;
    char size_buffer[MAX_SIZE_STRING]; // Buffer to store the size string
    int size_index = 0;
    int size;

    // Generate tag number
    tag_number = generate_tag();

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Send LOGIN command
    sprintf(buffer, "%s LOGIN %s %s\r\n", tag, username, password);
    tag_number += 1;
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
    }

    // Select the specified folder
    memset(recvline, 0, MAX_BUFFER_SIZE);
    sprintf(buffer, "%s SELECT \"%s\"\r\n", tag,
            folder); // Enclose folder name in double quotes
    tag_number += 1;
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
    }

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Check if messageNum is specified
    if (messageNum == -1) {
        //messageNum = getMessageNum(sockfd, folder);
        exit(EXIT_FAILURE);
    }

    // Fetch the specified message
    sprintf(buffer, "%s FETCH %d BODY.PEEK[]\r\n", tag, messageNum);
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
    }

    // Read the response
    memset(recvline, 0, MAX_BUFFER_SIZE);
    int i = 0;
    int j = 0;
    while ((n = read(sockfd, recvline, 1)) >= 0) {
        if (foundClosingBracket) {
            // Resize the output buffer to accommodate more characters
            output = realloc(output, (i + 1) * sizeof(char));
            if (output == NULL) {
                // Handle realloc failure
                printf("Error: Memory allocation failed\n");
                exit(EXIT_FAILURE);
            }
            if (char_count > 1) {
                output[i++] = recvline[0];
            }
            char_count++;
            // Print only after the first closing bracket
            if (char_count == size + 1) {
                break;
            }
        }
        if (recvline[0] == '{' && !foundOpeningBracket) {
            foundOpeningBracket = 1;
        } else if (recvline[0] == '}' && foundOpeningBracket) {
            foundClosingBracket = 1;
            size_buffer[size_index] = '\0';
            size = atoi(size_buffer);
        } else if (foundOpeningBracket && !foundClosingBracket) {
            size_buffer[size_index++] =
                recvline[0]; // Store the character in the size buffer
        }
        if (!foundClosingBracket) {
            prefix = realloc(prefix, (j + 1) * sizeof(char));
            prefix[j++] = recvline[0];
            // Handle error conditions
            if (strstr(prefix, "Authentication failed") != NULL ||
                strstr(prefix, "Mailbox doesn't exist") != NULL ||
                strstr(prefix, "Invalid messageset") != NULL) {
                char *error_message = NULL;
                if (strstr(prefix, "Authentication failed") != NULL) {
                    error_message = "Login failure";
            
                } else if (strstr(prefix, "Mailbox doesn't exist") != NULL) {
                    error_message = "Folder not found";
               
                } else {
                    error_message = "Message not found";
             
                }
                output = (char *)malloc(strlen(error_message) + 1);
                strcpy(output, error_message);
                printf("%s\n", output);
                exit(EXIT_CODE_3);
                return output;
            }
        }
    }

    if (n < 0) {
        printf("FAILED");
    }
    return output;
}

/*This function prints the subject lines of all the emails in a specified folder
and it sorts them by the message sequence number.*/
void list(int sockfd, char *username, char *password, char *folder) {
    char buffer[MAX_LIST_SIZE];
    char recvline[MAX_LIST_SIZE];
    int tag_number = generate_tag();
    char tag[MAX_TAG_SIZE];
    int n;

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Login to the server
    sprintf(buffer, "%s LOGIN %s %s\r\n", tag, username, password);
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
        return;
    }
    tag_number++;
    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    memset(recvline, 0, sizeof(recvline));
    // Read server response
    while ((n = read(sockfd, recvline, sizeof(recvline) - 1)) > 0) {
        if (strstr(recvline, "OK") != NULL) {
            break; // Exit loop if login successful
        }
    }

    // Select the folder
    sprintf(buffer, "%s SELECT \"%s\"\r\n", tag, folder);
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
        return;
    }
    tag_number++;
    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    memset(recvline, 0, sizeof(recvline));
    // Read server response
    while ((n = read(sockfd, recvline, sizeof(recvline) - 1)) > 0) {
        if (strstr(recvline, "OK") != NULL) {
            break; // Exit loop if folder selected successfully
        }
    }

    // Fetch email headers
    sprintf(buffer, "%s FETCH 1:* BODY.PEEK[HEADER.FIELDS (SUBJECT)]\r\n", tag);
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
        return;
    }
    tag_number += 1;
    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    while ((n = read(sockfd, recvline, sizeof(recvline) - 1)) > 0) {
        // Check if the end of email headers is reached
        if (strstr(recvline, "\r\n\r\n") != NULL) {
            // End of email headers detected, exit the loop
            break;
        }
    }

    int messageCount = 1;
    // Locate the byte count
    char *byteCountStart = strstr(recvline, "{");
    char *byteCountEnd = strstr(recvline, "}");
    char *start = strstr(recvline, "Subject: ");
    while (start != NULL) {
        // Find the start of the subject line
        start += strlen("Subject: ");

        if (byteCountStart != NULL && byteCountEnd != NULL) {
            // Extract the byte count

            int byteCount = atoi(byteCountStart + 1) - strlen("Subject: ");
            printf("%d: ", messageCount);
            for (int i = 0; i < byteCount; i++) {
                if (start[i] == '\r' || start[i] == '\n') {
                    // Skip printing '\r' and '\n' characters
                    byteCount -= 1;
                    continue;

                } else {
                    putchar(start[i]);
                }
            }
            putchar('\n');
            messageCount++;

            // Move to the next subject line
            byteCountStart = strstr(start, "{");
            byteCountEnd = strstr(byteCountStart, "}");
        }
        // Move to the next subject line
        start = strstr(start + 1, "Subject: ");
        if (start == NULL) {
            // No byte count found, treat as no subject
            printf("%d: <No subject>\n", messageCount);
            messageCount++;
        }
    }
}

int getMessageNum(int sockfd, char *folder) {
    char buffer[MAX_BUFFER_SIZE];
    char recvline[MAX_BUFFER_SIZE];
    int n, lastAddedMessageNum = 0;

    // Read message from stdin
    char tag[MAX_TAG_SIZE];
    // Generate tag number
    int tag_number = generate_tag();

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Send SELECT command to the server
    sprintf(buffer, "%s SELECT \"%s\"\r\n", tag, folder);
    tag_number += 1;
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
        return -1; // Return -1 to indicate failure
    }

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Read server response
    while ((n = read(sockfd, recvline, sizeof(recvline) - 1)) > 0) {
        recvline[n] = '\0'; // Null-terminate the received data
        // Check if the response contains the UIDNEXT information
        if (strstr(recvline, "[UIDNEXT ") != NULL) {
            char *uidNextStr = strstr(recvline, "[UIDNEXT ");
            int uidNext = atoi(uidNextStr + 9); // Extract the UIDNEXT value
            lastAddedMessageNum = uidNext - 1;  // Decrement UIDNEXT
            break; // Exit loop once UIDNEXT is found
        }
    }

    if (n < 0) {
        printf("FAILED");
        return -1; // Return -1 to indicate failure
    }

    return lastAddedMessageNum;
}

/*This function retrieves the main headers of a specified email, obtained through the
tags provided by the email.*/
char *parse(int sockfd, int messageNum) {
    char buffer[MAX_BUFFER_SIZE];
    char recvline[MAX_BUFFER_SIZE];
    int tag_number, n;
    char tag[MAX_TAG_SIZE];
    char *prefix = NULL;
    int foundOpeningBracket = 0;
    int foundClosingBracket = 0;
    char size_buffer[MAX_SIZE_STRING];
    int size_index = 0;
    int headers_finished =
        0; // Flag to indicate when all headers have been parsed
    int size;
    int char_count = 0;
    char *output = NULL;
    int i = 0;
    int j = 0;

    // Generate tag number
    tag_number = generate_tag();

    // Convert tag_number to string and concatenate with 'A'
    sprintf(tag, "A%d", tag_number);

    // Construct the IMAP command to fetch the header fields
    sprintf(buffer,
            "%s FETCH %d BODY.PEEK[HEADER.FIELDS (FROM TO DATE SUBJECT)]\r\n",
            tag, messageNum);

    // Send the IMAP command to the server
    if (write(sockfd, buffer, strlen(buffer)) != strlen(buffer)) {
        printf("ERROR sending message");
        return NULL;
    }

    // Read the response from the server
    while ((n = read(sockfd, recvline, 1)) > 0) {

        recvline[n] = '\0'; // Null-terminate the received data
        //  Check for the end of the response
        if (strstr(recvline, "FETCH completed") != NULL) {
            break;
        }

        // Check for the beginning of the email body
        if (strstr(recvline, "\r\n\r\n") != NULL) {
            headers_finished = 1;
            continue; // Skip the empty line and process the body
        }
        if (!headers_finished && foundClosingBracket) {
            if (output == NULL) {
                // Handle realloc failure
                printf("Error: Memory allocation failed\n");
                exit(EXIT_FAILURE);
            }
            if (char_count > 1) {
                output[i++] = recvline[0];
            }
            char_count++;

            if (char_count == size + 1) {
                break;
            }
        }
        if (recvline[0] == '{' && !foundOpeningBracket) {
            foundOpeningBracket = 1;
        } else if (recvline[0] == '}' && foundOpeningBracket) {
            foundClosingBracket = 1;
            size_buffer[size_index] = '\0';
            size = atoi(size_buffer);
            output = (char *)calloc(size + 1, sizeof(char));
        } else if (foundOpeningBracket && !foundClosingBracket) {
            size_buffer[size_index++] =
                recvline[0]; // Store the character in the size buffer
        }
        if (!foundClosingBracket) {
            prefix = realloc(prefix, (j + 1) * sizeof(char));
            prefix[j++] = recvline[0];
            if (strstr(prefix, "Authentication failed") != NULL) {
                char loginFail[14] = "Login failure";
                output = (char *)malloc(strlen(loginFail) + 1);
                strcpy(output, loginFail);
                printf("%s\n", output);
                exit(EXIT_CODE_3);
            }
            if (strstr(prefix, "Mailbox doesn't exist") != NULL) {
                char no_mail[17] = "Folder not found";
                output = (char *)malloc(strlen(no_mail) + 1);
                strcpy(output, no_mail);
                printf("%s\n", output);
                exit(EXIT_CODE_3);
            }
        }
    }

    if (n < 0) {
        printf("FAILED");
    }

    output = removeNewlines(output);
    printFrom(output);
    printTo(output);
    printDate(output);
    printSubject(output);

    return output;
}

/*This function decodes MIME messages and prints the ASCII version of the message*/
void mime(char *raw_email) {
    // Find the boundary parameter
    char *boundary_start = strstr(raw_email, "boundary=");
    if (boundary_start == NULL) {
        printf("Error: Boundary parameter not found\n");
        exit(EXIT_CODE_4);
    }

    boundary_start += strlen("boundary=");

    // Read the boundary value with or without quotation marks
    char boundary[MAX_BUFFER_SIZE]; // Assuming boundary value won't exceed 255 characters
    if (sscanf(boundary_start, " \"%255[^\"]\"", boundary) != 1) {
        // If scanning with quotation marks fails, try scanning without them
        if (sscanf(boundary_start, " %255[^ \n]", boundary) != 1) {
            printf("Error: Unable to read boundary value\n");
            exit(EXIT_CODE_4);
        }
    }

    // Find the start of the desired content part
    char *part_start = strstr(raw_email, "Content-Type: text/plain");
    if (part_start == NULL) {
        printf("Error: Desired content part not found\n");
        exit(EXIT_CODE_4);
    }
    part_start += strlen("Content-Type: text/plain");

    // Initialize pointers for charset and encoding
    char *charset_start = NULL;
    char *encoding_start = NULL;

    // Find charset and encoding positions
    charset_start = strstr(raw_email, "charset=UTF-8");
    encoding_start =
        strstr(raw_email, "Content-Transfer-Encoding: quoted-printable");

    // Check which one comes first
    if (charset_start && encoding_start) {
        // Check if charset comes first
        if (charset_start < encoding_start) {
            // Move part_start to the end of charset line
            part_start = strstr(encoding_start, "\n") + 1;
        } else {
            // Move part_start to the end of encoding line
            part_start = strstr(charset_start, "\n") + 1;
        }
    } else if (charset_start) {
        // Only charset found
        part_start = strstr(charset_start, "\n") + 1;
    } else if (encoding_start) {
        // Only encoding found
        part_start = strstr(encoding_start, "\n") + 1;
    } else {
        // Neither charset nor encoding found
        printf("Error: Charset or encoding not found\n");
        exit(EXIT_CODE_4);
    }

    // Find the end of the current header line
    char *line_end = strstr(part_start, "\r\n");
    if (line_end == NULL) {
        printf("Error: End of header line not found\n");
        exit(EXIT_CODE_4);
    }

    // Move past the current header line
    part_start = line_end + strlen("\r\n"); // Move past "\r\n"

    // Find the boundary of the next part
    char *boundary_next = strstr(part_start, boundary);
    if (boundary_next == NULL) {
        printf("Error: End boundary not found in email body\n");
        exit(EXIT_CODE_4);
    }

    // Find the last newline character before the boundary
    char *last_newline = NULL;
    char *last_char = NULL;
    char *search_ptr = part_start;
    while (search_ptr < boundary_next) {
        char *newline_ptr = strstr(search_ptr, "\r\n");
        if (newline_ptr != NULL && newline_ptr < boundary_next) {
            last_newline = newline_ptr;
            search_ptr =
                newline_ptr + 1; // Move search pointer past the newline
        } else {
            break; // No more newline characters before the boundary
        }
    }

    // Set last_char to the character after the last newline before the boundary
    if (last_newline != NULL) {
        // printf("Here\n");
        last_char = last_newline;
    } else {
        // If no newline character was found, set last_char to the beginning of
        // the content part
        last_char = part_start;
    }

    // Allocate memory for the content part string
    size_t length = last_char - part_start;
    char *content = (char *)malloc(length + 1); // +1 for null terminator
    if (content == NULL) {
        printf("Error: Memory allocation failed\n");
        exit(EXIT_CODE_4);
    }

    // Copy the content part to the allocated memory
    strncpy(content, part_start, length);
    length += 1;
    content[length] = '\0';
    printf("%s", content);
}

int generate_tag() {
    // Seed the random number generator for better randomness
    srand(time(NULL));

    // Generate a random number between 100 and 999
    int random_number = rand() % 900 + 100;

    return random_number;
}

char *removeNewlines(const char *str) {
    const char *src = str;
    char *dst;
    char *result;
    int length = strlen(str);

    // Allocate memory for the result string
    result = (char *)malloc(length + 1);
    if (result == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(1);
    }

    dst = result;
    while (*src) {
        if (*src != '\n' && *src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0'; // Null-terminate the modified string

    return result;
}

void printTo(char *input) {
    printf("\nTo:");
    int found = 0;
    for (int i = 0; i < strlen(input); i++) {
        char inputChar =
            tolower(input[i]); // Convert input character to lowercase

        // Check for the presence of "Subject:", "From:", or "Date:" to break
        // out of loop
        if (found >= 1 &&
            ((inputChar == 's' && tolower(input[i + 1]) == 'u' &&
              tolower(input[i + 2]) == 'b' && tolower(input[i + 3]) == 'j' &&
              tolower(input[i + 4]) == 'e' && tolower(input[i + 5]) == 'c' &&
              tolower(input[i + 6]) == 't' && tolower(input[i + 7]) == ':') ||
             (inputChar == 'f' && tolower(input[i + 1]) == 'r' &&
              tolower(input[i + 2]) == 'o' && tolower(input[i + 3]) == 'm' &&
              tolower(input[i + 4]) == ':') ||
             (inputChar == 'd' && tolower(input[i + 1]) == 'a' &&
              tolower(input[i + 2]) == 't' && tolower(input[i + 3]) == 'e' &&
              tolower(input[i + 4]) == ':'))) {
            break;
        }
        // Check for the presence of "To:" or if "found" flag is set
        if ((inputChar == 't' && tolower(input[i + 1]) == 'o' &&
             input[i + 2] == ':') ||
            found >= 1) {
            if (found >= 3) {
                printf("%c", input[i]);
            }
            found += 1;
        }
    }
}
void printFrom(char *input) {
    int found = 0;
    printf("From:");
    for (int i = 0; i < strlen(input); i++) {
        char inputChar =
            tolower(input[i]); // Convert input character to lowercase

        // Check for the presence of "Subject:", "To:", or "Date:" to break out
        // of loop
        if (found >= 1 &&
            ((inputChar == 's' && tolower(input[i + 1]) == 'u' &&
              tolower(input[i + 2]) == 'b' && tolower(input[i + 3]) == 'j' &&
              tolower(input[i + 4]) == 'e' && tolower(input[i + 5]) == 'c' &&
              tolower(input[i + 6]) == 't' && tolower(input[i + 7]) == ':') ||
             (inputChar == 't' && tolower(input[i + 1]) == 'o' &&
              tolower(input[i + 2]) == ':') ||
             (inputChar == 'd' && tolower(input[i + 1]) == 'a' &&
              tolower(input[i + 2]) == 't' && tolower(input[i + 3]) == 'e' &&
              tolower(input[i + 4]) == ':'))) {
            break;
        }
        // Check for the presence of "From:" or if "found" flag is set
        if ((inputChar == 'f' && tolower(input[i + 1]) == 'r' &&
             tolower(input[i + 2]) == 'o' && tolower(input[i + 3]) == 'm' &&
             tolower(input[i + 4]) == ':') ||
            found >= 1) {
            if (found >= 5) {
                printf("%c", input[i]);
            }

            found += 1;
        }
    }
}

void printDate(char *input) {
    printf("\nDate:");
    int found = 0;
    for (int i = 0; i < strlen(input); i++) {
        char inputChar =
            tolower(input[i]); // Convert input character to lowercase

        // Check for the presence of "Subject:", "To:", or "From:" to break out
        // of loop
        if ((found >= 1 &&
             ((inputChar == 's' && tolower(input[i + 1]) == 'u' &&
               tolower(input[i + 2]) == 'b' && tolower(input[i + 3]) == 'j' &&
               tolower(input[i + 4]) == 'e' && tolower(input[i + 5]) == 'c' &&
               tolower(input[i + 6]) == 't' && tolower(input[i + 7]) == ':') ||
              (inputChar == 't' && tolower(input[i + 1]) == 'o' &&
               tolower(input[i + 2]) == ':') ||
              (inputChar == 'f' && tolower(input[i + 1]) == 'r' &&
               tolower(input[i + 2]) == 'o' && tolower(input[i + 3]) == 'm' &&
               tolower(input[i + 4]) == ':'))) ||
            inputChar == '\n') {
            break;
        }
        // Check for the presence of "Date:" or if "found" flag is set
        if ((inputChar == 'd' && tolower(input[i + 1]) == 'a' &&
             tolower(input[i + 2]) == 't' && tolower(input[i + 3]) == 'e' &&
             input[i + 4] == ':') ||
            found >= 1) {
            if (found >= 5) {
                printf("%c", input[i]);
            }
            found += 1;
        }
    }
}

void printSubject(char *input) {
    printf("\nSubject:");
    int found = 0;
    for (int i = 0; i < strlen(input); i++) {
        char inputChar =
            tolower(input[i]); // Convert input character to lowercase

        // Check for the presence of "Date:", "To:", or "From:" to break out of
        // loop
        if (found >= 1 &&
            ((inputChar == 'd' && tolower(input[i + 1]) == 'a' &&
              tolower(input[i + 2]) == 't' && tolower(input[i + 3]) == 'e' &&
              tolower(input[i + 4]) == ':') ||
             (inputChar == 't' && tolower(input[i + 1]) == 'o' &&
              tolower(input[i + 2]) == ':') ||
             (inputChar == 'f' && tolower(input[i + 1]) == 'r' &&
              tolower(input[i + 2]) == 'o' && tolower(input[i + 3]) == 'm' &&
              tolower(input[i + 4]) == ':'))) {
            break;
        }
        // Check for the presence of "Subject:" or if "found" flag is set
        if ((inputChar == 's' && tolower(input[i + 1]) == 'u' &&
             tolower(input[i + 2]) == 'b' && tolower(input[i + 3]) == 'j' &&
             tolower(input[i + 4]) == 'e' && tolower(input[i + 5]) == 'c' &&
             tolower(input[i + 6]) == 't' && tolower(input[i + 7]) == ':') ||
            found >= 1) {
            if (found >= 8) {
                printf("%c", input[i]);
            }
            found += 1;
        }
    }
    if (found == 0) {
        printf(" <No subject>");
    }
    printf("\n");
}
