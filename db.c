#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
#define TABLE_MAX_PAGES 100
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

//Structure to hold our CommandResult for example if a command is not found it will return META_COMMAND_UNRECOGNIZED_COMMAND but if command exists it will return META_COMMAND_SUCCESS
//We do this to warn the user that the command does not exist and we use it for error handling
typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
     STATEMENT_INSERT, STATEMENT_SELECT 
    } StatementType;

typedef enum { 
    EXECUTE_SUCCESS, EXECUTE_TABLE_FULL 
} ExecuteResult;

//Commands to handle different types of success or errors
//Why we do this is to try to prevent the program breaking even more or when a type of PrepareResult is sent it prints out an custom message
typedef enum PrepareResult {
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

//Structure to hold id, username, email
//Why we do this is to be able to store our id, username, email if there was no struct the program would most likely crash
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

//Structure to hold File information like the file_descriptor which the OS gives you back a number and you can use that number to read or write or both or create a file if it does not exist
//We do this to store it into an actual database file so we can use it for later
typedef struct {
    int file_descriptor;
    uint32_t file_length;
    void* pages[TABLE_MAX_PAGES];
} Pager;


const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t PAGE_SIZE = 4096;
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct Table {
    uint32_t num_rows;
    Pager* pager;
} Table;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

//Function declarations
//We do this so we can tell the compiler hey the function exists
InputBuffer* new_input_buffer(void);
void close_input_buffer(InputBuffer* input_buffer);
void print_prompt(void);
void read_input(InputBuffer* input_buffer);
void print_row(Row* row);
void serialize_row(Row* source, void* destination);
void* row_slot(Table* table, uint32_t row_num);
void deserialize_row(void* source, Row* destination);
ExecuteResult execute_insert(Statement* statement, Table* table);
ExecuteResult execute_select(Statement* statement, Table* table);
void free_table(Table* table);
Table* db_open(const char* filename);
Pager* pager_open(const char* filename);
void* get_page(Pager* pager, uint32_t page_num);
void pager_flush(Pager* pager, uint32_t page_num, uint32_t size);

Table* db_open(const char* filename){
    //1. Open the database file and get a pager
    Pager* pager = pager_open(filename);

    uint32_t num_rows = 0;

    //2. Checks if its greater then 0 if so then check  how many rows it has
    if (pager->file_length > 0){
        //Count rows in complete pages
        num_rows = pager->file_length / PAGE_SIZE * ROWS_PER_PAGE;

        //Checks if there is a partial page at the end
        uint32_t remainder = pager->file_length % PAGE_SIZE;
        if (remainder > 0){
            // Add rows from partial page
            num_rows += remainder / ROW_SIZE;
        }
    }

    // 3. Create the table structure in memory
    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    table->num_rows = num_rows;

    return table;
}

//Open a database file using pager
Pager* pager_open(const char* filename){
    //This stores our flags like write, read, create, close
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);

    //Checks if file failed to open (fd = -1 means error)
    if (fd == -1){
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    //Creates memory in RAM for Pager
    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;

    //Sets index of pages to NULL
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++){
        pager->pages[i] = NULL;
    }

    return pager;
}

//Gets page using pager
void* get_page(Pager* pager, uint32_t page_num){
    //Checks if TABLE_MAX_PAGES is greated then page_num if yes then return EXIT_FAILURE
    if (page_num > TABLE_MAX_PAGES){
        printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    //Checks if pages struct and inside that page_num and if it equals to NULL if not then we just return it
    if (pager->pages[page_num] == NULL){
        void* page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager->file_length / PAGE_SIZE;
        if (pager->file_length % PAGE_SIZE){
            num_pages += 1;
        }

        if (page_num <= num_pages){
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1){
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;
    }

    return pager->pages[page_num];
}


InputBuffer* new_input_buffer(){
    //Creates a new_input_buffer and allocates it in RAM
    InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));

    //Checks if malloc failed usually if malloc fails it returns NULL so were checking if it returned NULL if so it failed
    if (input_buffer == NULL){
      fprintf(stderr, "Error: malloc failed\n");
      exit(EXIT_FAILURE);
    }
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}


//Checks input_buffer for MetaCommands
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table){
    //If user types .exit it will close the program and return with EXIT_SUCCESS;
    if (strcmp(input_buffer->buffer, ".exit") == 0){
        close_input_buffer(input_buffer);
        free_table(table);
        exit(EXIT_SUCCESS);
    } else if (strcmp(input_buffer->buffer, ".help") == 0){
         printf("Available commands:\n");
         printf(" .exit    - Exit the database\n");
         printf(" .help    - Show this help message\n");
         printf(" insert   - Insert a row (insert <id> <username> <email>)\n");
         return META_COMMAND_SUCCESS;
    } else {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
    
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
    if (strncmp(input_buffer->buffer, "insert", 6) == 0){
        statement->type = STATEMENT_INSERT;

        char* keyword = strtok(input_buffer->buffer, " ");
        char* id_string = strtok(NULL, " ");
        char* username = strtok(NULL, " ");
        char* email = strtok(NULL, " ");

        if (id_string == NULL || username == NULL || email == NULL){
            return PREPARE_SYNTAX_ERROR;
        }

        int id = atoi(id_string);
        if (id < 0) return PREPARE_NEGATIVE_ID;
        if (strlen(username) > COLUMN_USERNAME_SIZE) return PREPARE_STRING_TOO_LONG;
        if (strlen(email) > COLUMN_EMAIL_SIZE) return PREPARE_STRING_TOO_LONG;

        statement->row_to_insert.id = id;
        strcpy(statement->row_to_insert.username, username);
        strcpy(statement->row_to_insert.email, email);

        return PREPARE_SUCCESS;
    }

    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

//Shut down input_buffer stops getting input
void close_input_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

void print_prompt(){
    printf("db > ");
}

//Reads input from the InputBuffer
void read_input(InputBuffer* input_buffer){
    //Gets each line from the inputer_buffer
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0){
        if (feof(stdin)){
          printf("\nEnd of input reached.\n");
        } else {
          fprintf(stderr, "Error reading input: %s\n", strerror(errno));
        }
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
}

ExecuteResult execute_select(Statement* statement, Table* table){
    Row row;
    for (uint32_t i = 0; i < table->num_rows; i++){
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }
    return EXECUTE_SUCCESS;
}

//Checks our statement if user types insert command then return STATEMENT_INSERT if user types Select then return STATEMENT_SELECT
ExecuteResult execute_statement(Statement* statement, Table* table){
    switch (statement->type){
        case (STATEMENT_INSERT):
                   return execute_insert(statement, table);
        case (STATEMENT_SELECT):
           return execute_select(statement, table);
    }
}

//Function for insert command
ExecuteResult execute_insert(Statement* statement, Table* table){
    if (table->num_rows >= TABLE_MAX_ROWS){
        return EXECUTE_TABLE_FULL;
    }

    Row* row_to_insert = &(statement->row_to_insert);

    void* destination = row_slot(table, table->num_rows);

    serialize_row(row_to_insert, destination);

    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

// We Convert Row struct into raw bytes and then write to page memory
//We do this so we can store our Row struct into raw bytes and write it to page memory for later use
void serialize_row(Row* source, void* destination){
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

// Convert raw bytes from page memory back into a Row struct
void deserialize_row(void* source, Row* destination){
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// Find the memory address where a specific row is stored
//Calculates which page the row is on and the byte offset within that page
void* row_slot(Table* table, uint32_t row_num){
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void* page = get_page(table->pager, page_num);
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
}

//Writes data from RAM to the hard drive
void pager_flush(Pager* pager, uint32_t page_num, uint32_t size){
    //Checks if it equals to NULL pointer which stands for nothing in the pointer if so we exit the program
    if (pager->pages[page_num] == NULL){
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1){
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], size);
    if (bytes_written == -1){
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

//Frees our table memory that we allocated
void free_table(Table* table){
   Pager* pager = table->pager;
   uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

   // Flush full pages to disk
   for (uint32_t i = 0; i < num_full_pages; i++){
    if (pager->pages[i] == NULL){
        continue;
    }
    pager_flush(pager, i, PAGE_SIZE);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
   }

   uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
   if (num_additional_rows > 0){
    uint32_t page_num = num_full_pages;
    if (pager->pages[page_num] != NULL){
        pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
        free(pager->pages[page_num]);
        pager->pages[page_num] = NULL;
    }
   }

   for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++){
    void* page = pager->pages[i];
    if (page){
        free(page);
        pager->pages[i] = NULL;
    }
   }

   close(pager->file_descriptor);
   free(pager);
   free(table);
}


void print_row(Row* row){
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

//Main function to start program
int main(int argc, char* argv[]){
    //Checks if you gave a database filename if not then exit
    if (argc < 2){
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);
    
    InputBuffer* input_buffer = new_input_buffer();
    Statement statement;
    while (true) {
        print_prompt();
        read_input(input_buffer);
            //Checks if the first character  in the input_buffer is . then execute do_meta_command
            if (input_buffer->buffer[0] == '.'){
                switch (do_meta_command(input_buffer, table)){
                    case (META_COMMAND_SUCCESS): continue;
                    case (META_COMMAND_UNRECOGNIZED_COMMAND):
                        printf("Unrecognized command: '%s'.\n", input_buffer->buffer);
                        continue;
                }
            }

            switch (prepare_statement(input_buffer, &statement)){
                case (PREPARE_SUCCESS): break;
                case (PREPARE_STRING_TOO_LONG):
                   printf("String is too long.\n");
                   continue;
                case (PREPARE_SYNTAX_ERROR):
                     printf("Syntax error. Could not parse statement.\n");
                     continue;
                case (PREPARE_UNRECOGNIZED_STATEMENT):
                    printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
                    continue;
                case (PREPARE_NEGATIVE_ID):
                     printf("ID must be postive.\n");
                     continue;
            }

            switch (execute_statement(&statement, table)){
                case (EXECUTE_SUCCESS):
                    printf("Executed.\n");
                    break;
                case (EXECUTE_TABLE_FULL):
                     printf("Error: Table full.\n");
                     break;
            }
    }
}
