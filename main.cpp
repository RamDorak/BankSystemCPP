# include <iostream>
# include <mysql.h>
# include <iomanip>
# include <sstream>
# include <cstring>
# include <csignal>
#include<cstdlib>
#include "SHA512.h"

using namespace std;

#define SERVER std::getenv("SERVER")
#define USER std::getenv("USER")
#define PASSWORD std::getenv("PASSWORD")
#define DATABASE std::getenv("DATABASE")

int loggedInAccount = -1;

MYSQL* connectDB() {
    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        cerr << "MySQL initialization failed!" << endl;
        exit(1);
    }
    conn = mysql_real_connect(conn, SERVER, USER, PASSWORD, DATABASE, 0, NULL, 0);
    if (!conn) {
        cerr << "Database connection failed: " << mysql_error(conn) << endl;
        exit(1);
    }
    return conn;
}

void createTables(MYSQL* conn) {
    const char* loginTable = "CREATE TABLE IF NOT EXISTS LoginDetails ("
                             "accountId INT PRIMARY KEY AUTO_INCREMENT," 
                             "password VARCHAR(255) NOT NULL," 
                             "isActive BOOLEAN DEFAULT FALSE)";

    const char* accountTable = "CREATE TABLE IF NOT EXISTS AccountDetails ("
                               "accountId INT PRIMARY KEY," 
                               "username VARCHAR(255) NOT NULL," 
                               "balance DOUBLE DEFAULT 0," 
                               "accountType VARCHAR(50)," 
                               "FOREIGN KEY (accountId) REFERENCES LoginDetails(accountId))";
    
    mysql_query(conn, loginTable);
    mysql_query(conn, accountTable);
}

bool login(MYSQL* conn, int accountId, const string& password) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        cerr << "MySQL statement initialization failed!" << endl;
        return false;
    }

    const char* query = "SELECT isActive FROM LoginDetails WHERE accountId = ? AND password = ?";
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        cerr << "MySQL statement preparation failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &accountId;

    string hashedPassword = to_string(hash<string>{}(password));
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)hashedPassword.c_str();
    bind[1].buffer_length = hashedPassword.length();

    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) != 0) {
        cerr << "Execution failed: " << mysql_stmt_error(stmt) << endl;
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND resultBind;
    int isActive;
    memset(&resultBind, 0, sizeof(resultBind));
    resultBind.buffer_type = MYSQL_TYPE_LONG;
    resultBind.buffer = &isActive;

    mysql_stmt_bind_result(stmt, &resultBind);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) == 0) {
        if (isActive == 1) {
            cout << "Login denied! This account is already logged in." << endl;
            mysql_stmt_close(stmt);
            return false;
        }

        loggedInAccount = accountId;
        mysql_stmt_close(stmt);

        // Mark as active
        const char* updateQuery = "UPDATE LoginDetails SET isActive = 1 WHERE accountId = ?";
        stmt = mysql_stmt_init(conn);
        mysql_stmt_prepare(stmt, updateQuery, strlen(updateQuery));

        MYSQL_BIND updateBind;
        memset(&updateBind, 0, sizeof(updateBind));
        updateBind.buffer_type = MYSQL_TYPE_LONG;
        updateBind.buffer = &accountId;

        mysql_stmt_bind_param(stmt, &updateBind);
        mysql_stmt_execute(stmt);
        mysql_stmt_close(stmt);

        cout << "Login successful!" << endl;
        return true;
    }

    cout << "Invalid credentials!" << endl;
    mysql_stmt_close(stmt);
    return false;
}


void logout() {
    loggedInAccount = -1;
    cout << "Logged out successfully!" << endl;
}

void deposit(MYSQL* conn, double amount) {
    string query = "UPDATE AccountDetails SET balance = balance + " + to_string(amount) + " WHERE accountId = " + to_string(loggedInAccount);
    mysql_query(conn, query.c_str());
    cout << "Deposited " << amount << " successfully!" << endl;
}

void withdraw(MYSQL* conn, double amount) {
    string query = "SELECT balance FROM AccountDetails WHERE accountId = " + to_string(loggedInAccount);
    mysql_query(conn, query.c_str());
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    double balance = stod(row[0]);
    mysql_free_result(res);
    
    if (balance < amount) {
        cout << "Insufficient funds!" << endl;
        return;
    }
    query = "UPDATE AccountDetails SET balance = balance - " + to_string(amount) + " WHERE accountId = " + to_string(loggedInAccount);
    mysql_query(conn, query.c_str());
    cout << "Withdrawn " << amount << " successfully!" << endl;
}

void transfer(MYSQL* conn, int recipientId, double amount) {
    if (amount <= 0) {
        cout << "Invalid transfer amount!" << endl;
        return;
    }

    string query = "SELECT balance FROM AccountDetails WHERE accountId = " + to_string(loggedInAccount);
    if (mysql_query(conn, query.c_str()) != 0) {
        cerr << "Error fetching balance: " << mysql_error(conn) << endl;
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        cerr << "Error storing result: " << mysql_error(conn) << endl;
        return;
    }
    
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        cerr << "Error fetching row or account not found!" << endl;
        mysql_free_result(res);
        return;
    }
    
    double balance = stod(row[0]);
    mysql_free_result(res);
    
    if (balance < amount) {
        cout << "Insufficient funds!" << endl;
        return;
    }
    
    query = "SELECT accountId FROM AccountDetails WHERE accountId = " + to_string(recipientId);
    if (mysql_query(conn, query.c_str()) != 0) {
        cerr << "Error verifying recipient: " << mysql_error(conn) << endl;
        return;
    }
    
    res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        cerr << "Recipient account not found!" << endl;
        if (res) mysql_free_result(res);
        return;
    }
    mysql_free_result(res);

    // Start transaction
    if (mysql_query(conn, "START TRANSACTION") != 0) {
        cerr << "Error starting transaction: " << mysql_error(conn) << endl;
        return;
    }

    query = "UPDATE AccountDetails SET balance = balance - " + to_string(amount) + " WHERE accountId = " + to_string(loggedInAccount);
    if (mysql_query(conn, query.c_str()) != 0) {
        cerr << "Error deducting amount: " << mysql_error(conn) << endl;
        mysql_query(conn, "ROLLBACK");
        return;
    }

    query = "UPDATE AccountDetails SET balance = balance + " + to_string(amount) + " WHERE accountId = " + to_string(recipientId);
    if (mysql_query(conn, query.c_str()) != 0) {
        cerr << "Error adding amount to recipient: " << mysql_error(conn) << endl;
        mysql_query(conn, "ROLLBACK");
        return;
    }
    
    if (mysql_query(conn, "COMMIT") != 0) {
        cerr << "Error committing transaction: " << mysql_error(conn) << endl;
        mysql_query(conn, "ROLLBACK");
        return;
    }
    
    cout << "Transferred " << amount << " successfully!" << endl;
}

void registerUser(MYSQL* conn) {
    string username, password, accountType;
    cout << "Enter Username: "; cin >> username;
    cout << "Enter Password: "; cin >> password;

    std::hash<std::string> hasher;
    password = std::to_string(hasher(password));

    cout << "Enter Account Type: "; cin >> accountType;
    
    string query = "INSERT INTO LoginDetails (password) VALUES ('" + password + "')";
    if (mysql_query(conn, query.c_str()) == 0) {
        int accountId = mysql_insert_id(conn);
        query = "INSERT INTO AccountDetails (accountId, username, balance, accountType) VALUES (" + to_string(accountId) + ", '" + username + "', 0, '" + accountType + "')";
        mysql_query(conn, query.c_str());
        cout << "Account registered successfully! Your Account ID is " << accountId << endl;
    } else {
        cout << "Registration failed!" << endl;
    }
}

void exitProgram(MYSQL* conn) {
    if (loggedInAccount != -1) {
        string query = "UPDATE LoginDetails SET isActive = 0 WHERE accountId = " + to_string(loggedInAccount);
        if (mysql_query(conn, query.c_str()) == 0) {
            cout << "Account " << loggedInAccount << " logged out successfully!" << endl;
        } else {
            cerr << "Error logging out: " << mysql_error(conn) << endl;
        }
    } else {
        cout << "No user is logged in." << endl;
    }
    mysql_close(conn);
    exit(0);
}

void signalHandler(int signum) {
    MYSQL* conn = connectDB();
    exitProgram(conn);
}

int main() {
    signal(SIGINT, signalHandler); 

    MYSQL* conn = connectDB();
    createTables(conn);
    
    int choice, accountId;
    string password;
    double amount;
    int recipientId;
    
    while (true) {
        if (loggedInAccount == -1) {
            cout << "1. Login\n2. Register User\n3. Exit\nEnter choice: ";
        } else {
            cout << "1. Deposit\n2. Withdraw\n3. Transfer\n4. Logout\n5. Exit\nEnter choice: ";
        }
        cin >> choice;
        
        if (loggedInAccount == -1) {
            switch (choice) {
                case 1:
                    cout << "Enter Account ID: "; cin >> accountId;
                    cout << "Enter Password: "; cin >> password;
                    
                    std::hash<std::string> hasher;
                    login(conn, accountId, std::to_string(hasher(password)));

                    break;
                case 2:
                    registerUser(conn);
                    break;
                case 3:
                    exitProgram(conn);
                    return 0;
            }
        } else {
            switch (choice) {
                case 1:
                    cout << "Enter amount to deposit: "; cin >> amount;
                    deposit(conn, amount);
                    break;
                case 2:
                    cout << "Enter amount to withdraw: "; cin >> amount;
                    withdraw(conn, amount);
                    break;
                case 3:
                    cout << "Enter recipient ID: "; cin >> recipientId;
                    cout << "Enter amount to transfer: "; cin >> amount;
                    transfer(conn, recipientId, amount);
                    break;
                case 4:
                    exitProgram(conn);
                    logout();
                    break;
                case 5:
                    exitProgram(conn);
                    return 0;
            }
        }
    }
}
