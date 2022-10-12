/*
Created: 29/09/2022 7:40:44 PM
Author : Josh Dowling


EEPROM memory:
* Stores header and code 
* header is 24 bytes 
* codes are linked to header with 4 bytes 
	* Byte 0 is active - 1 for yes, 0 for no
	* Byte 1 is length of code 
	* Byte 2 is upper byte of pointer 
	* byte 2 is lower byte of pointer

* Header structure:	
	* Index 0-3: j o s h	header 
	* Index 4-7: admin code 
	* Index 8-11: code 1
	* Index 12-15: code 2
	* Index 16-19: code 3
	* Index 20-23: code 4


* Codes in memory are a linked list type structure of 4 bytes at a time 
	* Byte 0 is active - CC for yes and anything else for no 
	* Byte 1 is data
	* Byte 2 is upper byte of pointer to next element
	* Byte 3 is lower byte of pointer to next element
*/


/*
UI Changes
- Clear key after error
- clear code after error 
- fix state order
*/
#include <avr/io.h>

#define format getKey
//#define format simKey

#define MINLEN 5

#define MAXLEN 50

#define LOCKTIME 10

enum states {initial, userLocked, adminLocked, adminUnlocked, safeSelect, userUnlocked, editCode, lockout}; 
	
//*****************************************************************************************************************************************************************//
// FUNCTIONS
//*****************************************************************************************************************************************************************//
void err();
void delay(int ms);
int8_t getKey();
void releaseKey(int8_t key);
int8_t simKey();
unsigned char EEPROM_read(unsigned int uiAddress);
void EEPROM_write(unsigned int uiAddress, unsigned char ucData);
	
//*****************************************************************************************************************************************************************//
// CLASSES
//*****************************************************************************************************************************************************************//

//*****************************************************************************************//
// Lock class 
// Contains information on 1 code and the functions to interact with it
//*****************************************************************************************//
class lock {
	private:
		int8_t code[MAXLEN];
		uint8_t code_length;
		int8_t active;
	public:
		lock() {code_length=0, active = 0;}	
		void set_code(); 
		void set_active(int8_t num); 
		int8_t get_active() {return active; }
		int8_t compare_code(lock * comp); 
		int8_t get_code_len() {return code_length;}
		int8_t get_code_digit(int8_t num); 
		void update_code(lock * new_code, int8_t pos);
		void del_code(int8_t pos); 
		void read_code(int8_t pos); 
};

void lock::set_active(int8_t num){
	if(num >=0 && num <= 2){
		active = num; 
		if(num != 1){
			code_length = 0; 
		}
	}
	else {
		while(1); 
	}
}

int8_t lock::compare_code(lock * comp){
	if(code_length != comp->get_code_len()){
		comp->set_active(0); 
		return 0; 
	}
	for(uint8_t i = 0; i< code_length; i++){
		if(code[i] != comp->get_code_digit(i)){
			comp->set_active(0); 
			return 0;
		}
	}
	comp->set_active(0); 
	return 1; 
}

int8_t lock::get_code_digit(int8_t num){
	if(num<code_length){
		return code[num];
	}
	else {
		while(1); 
	}
}

void lock::del_code(int8_t pos){
	unsigned int point = (pos*4)+4;
	unsigned char addr[2]; 
	for(int8_t i=0; i<=code_length;i++){
		EEPROM_write(point, 0); 
		point = point+2;
		addr[1] = EEPROM_read(point);
		point++;
		addr[0] = EEPROM_read(point);
		point = addr[0];
		point = point | (addr[1]<<8);
	}
}

void lock::read_code(int8_t pos){
	unsigned int point = (pos*4)+4; 
	unsigned char test = EEPROM_read(point); 
	if(test == 1){
		active = 1; 
		point++; 
		test = EEPROM_read(point);
		code_length = test; 
		point++;
		unsigned char addr[2];
		for(int8_t i=0; i< code_length; i++){
			addr[1] = EEPROM_read(point);
			point++;
			addr[0] = EEPROM_read(point);
			point = addr[0];
			point = point | (addr[1]<<8);
			point++;
			test = EEPROM_read(point); 
			code[i] = test; 
			point++; 
		}
		
	}
}

//*****************************************************************************************//
// lock box class 
// contains many lock objects and acts as a collection of them in 1 place 
//*****************************************************************************************//
class lock_box {
	private:
		int8_t select; 
		int8_t attempt; 
	public:
		lock_box(){select = 0; attempt=0;}
			
		lock safe[4];
		lock admin_code;
		lock temp_code;
		void set_select(int8_t num);
		
		int8_t compare_code(uint8_t code_sel); 
		void update_code(uint8_t code_sel); 
		int8_t check_active();
		void set_active(int8_t num);
		void del_code();
};

void lock_box::set_select(int8_t num){
	if(num >= 0 && num <= 3){
		select = num; 
	}
	else{
		while(1); 
	}
}

int8_t lock_box::compare_code(uint8_t code_sel){
	volatile int8_t val; 
	if(code_sel == 0){
		val = safe[select].compare_code(&temp_code);
	}
	else if(code_sel == 1){
		val = admin_code.compare_code(&temp_code);  
	}
	if(val == 0){
		attempt++;
		if(attempt == 3){
			attempt=0;
			return 2;
		}
	}
	return val; 
}

void lock_box::update_code(uint8_t code_sel){
	if(code_sel == 0){
		safe[select].update_code(&temp_code, select+1);
	}
	else if(code_sel == 1){
		admin_code.update_code(&temp_code, 0);
	}
	else {
		while(1);
	}
}

int8_t lock_box::check_active(){
	return safe[select].get_active(); 
}

void lock_box::set_active(int8_t num){
	safe[select].set_active(num); 
}

void lock_box::del_code(){
	safe[select].del_code(select+1); 
}

//*****************************************************************************************//
// state machine class
// contains lock box object 
// contains all state functions and switches between them 
//*****************************************************************************************//
class state_machine {
	private:
		states current_state;
		states last_state; 
		
		lock_box digi_safe;
		
		void set_state(states next_state);
		void back_state(); 
		void initial_state(); 
		void user_locked_state(); 
		void admin_locked_state(); 
		void admin_unlocked_state(); 
		void safe_select_state(); 
		void user_unlocked_state(); 
		void edit_code_state(); 
		void lockout_state(); 
	public:
		state_machine() {current_state = initial; }
			
		void run_state(); 	
		
};

void state_machine::set_state(states next_state){
	last_state = current_state;
	current_state = next_state;
	PORTB = current_state << 4;
}

void state_machine::back_state(){
	states temp = current_state; 
	current_state = last_state; 
	last_state = temp; 
	PORTB = current_state << 4;
}

//*****************************************************************************************************************************************************************//
// MAIN
//*****************************************************************************************************************************************************************//
int main(void)
{
    DDRB = 0xFF;
    DDRC = 0xF0;
	
	state_machine state; 

    while (1) 
    {
		state.run_state(); 
	}
}
//*****************************************************************************************************************************************************************//
// STATE CLASS FUNCTIONS
//*****************************************************************************************************************************************************************//


//*****************************************************************************************//
// Run state method
// Class: State machine 
// calls function based on the current state variable 
//*****************************************************************************************//
void state_machine::run_state(){
	switch (current_state){
		case initial:
			initial_state();
			break;
		case userLocked:
			user_locked_state();
			break;
		case adminLocked:
			admin_locked_state(); 
			break;
		case adminUnlocked:
			admin_unlocked_state(); 
			break;
		case safeSelect:
			safe_select_state(); 
			break;
		case userUnlocked:
			user_unlocked_state(); 
			break;
		case editCode:
			edit_code_state(); 
			break;
		case lockout:
			lockout_state(); 
			break;
	}
}

//*****************************************************************************************//
// Initial State
// Sets up header on EEPROM to validate that the correct memory is there
// I/O: none
// Linked states: Admin locked, user locked
//*****************************************************************************************//
void state_machine::initial_state(){
	
	int8_t header[5] = {0x6A, 0x6F, 0x73, 0x68, 1}; //ASCII for j o s h, 1 checks for valid admin code 
	int8_t read;
	unsigned int i;
	volatile int8_t valid = 1; //valid flag
	for(i=0; i<sizeof(header); i++){ //go through header
		read = EEPROM_read(i);
		if(read != header[i]){
			valid =0;
			EEPROM_write(i, header[i]);
		}
	}
	
	
	if(valid){ //if the header was there
		//load codes and data
		digi_safe.admin_code.read_code(0); 
		for(int8_t j=0; j<4; j++){
			digi_safe.safe[j].read_code(j+1); 
		}
		set_state(userLocked); 
	}
	else { //if the header was not there
		for(i = 1; i<=5; i++){
			EEPROM_write(i*4, 0);
			EEPROM_write((i*4)+1,0);
		}
		set_state(adminLocked); 
	}
}

//*****************************************************************************************//
// User locked state
// State where user must select a safe to use
// Inputs: key from user via keypad
// Outputs: None
// Linked States: Admin Locked, safe Select
//*****************************************************************************************//
void state_machine::user_locked_state(){
	int8_t key = format(); //break
	//int8_t key = simKey();
	if(key != -1){
		if(key == 10){ //if Astrix
			set_state(adminLocked);
		}
		else if (key <= 11){ //if number or hash
			err();  
		}
		else if(key > 11 && key <= 15){
			digi_safe.set_select(key-12); 
			set_state(safeSelect);
		}
	}
}

//*****************************************************************************************//
// Admin locked state
// wait for admin code to get to admin mode
// I/O: None
// Linked states:, User locked, admin unlocked
//*****************************************************************************************//
void state_machine::admin_locked_state(){
	digi_safe.temp_code.set_code(); //break
	if (digi_safe.temp_code.get_active()==1){ //code finished
		if(digi_safe.admin_code.get_active()){ //if admin code present
			int8_t ret = digi_safe.compare_code(1); 
			if(ret==1){
				set_state(adminUnlocked); 
			}
			else if(ret == 2){
				set_state(lockout);
			}
			else{
				err(); 
			}
		}
		else { //if no code
			digi_safe.update_code(1); 
			set_state(adminUnlocked); 
		}
	}
	else if(digi_safe.temp_code.get_active()==2){ //cancel
		digi_safe.temp_code.set_active(0); 
		if(digi_safe.admin_code.get_active()==1){
			set_state(userLocked);
		}
	}
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void state_machine::admin_unlocked_state(){
	int8_t key = format(); //break
	//int8_t key = simKey();
	if(key != -1){
		if(key == 10){ // *
			//go back
			set_state(adminLocked); 
		}
		else if(key == 1){
			// go to user locked
			set_state(userLocked);
		}
		else if(key == 2){
			// reset system
		}
		else if(key == 3){
			// edit admin code
			set_state(editCode);
		}
		else if(key >11 && key <=15){//key is a letter
			// go into that safe
			digi_safe.set_select(key-12);
			set_state(userUnlocked);
		}
		else {
			err();
		}	
	}
}

//*****************************************************************************************//
// safe select state 
// class: state machine 
// Gets code from user and either unlocks safe if in use or applies that code to safe if not
// Inputs: code form user
// outputs: none
//*****************************************************************************************//
void state_machine::safe_select_state(){
	digi_safe.temp_code.set_code();  //break
	if(digi_safe.temp_code.get_active() == 1){ //Code complete 
		//check if safe is in use 
		if(digi_safe.check_active()){ //is active 
			int8_t ret = digi_safe.compare_code(0);
			if(ret == 1){
				set_state(userUnlocked);
			}
			else if(ret == 2){
				set_state(lockout);
			}
			else{ //Code is wrong
				err(); 
			}
		}
		else{//is not active 
			digi_safe.update_code(0);
			set_state(userUnlocked); 
		}
		
		
	}
	else if(digi_safe.temp_code.get_active() == 2){ //* to cancel 
		digi_safe.temp_code.set_active(0);
		set_state(userLocked);  
	}	
}

//*****************************************************************************************//
// user unlocked state
// class: state machine
// This is the user being in the safe
// they can go back, lock the safe, release the safe or edit the code 
//*****************************************************************************************//
void state_machine::user_unlocked_state(){
	int8_t key = format(); //break
	//int8_t key = simKey();
	if(key != -1){
		if(key == 10){ // * so go back
			back_state(); 
		}
		else if(key == 1){ //lock safe
			set_state(userLocked); 
		}
		else if(key == 2){//release safe
			digi_safe.del_code(); 
			digi_safe.set_active(0); 
			set_state(userLocked); 
		}
		else if(key == 3){ //edit code
			set_state(editCode); 
		}
		else {
			err(); 
		}
	}	
	
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void state_machine::edit_code_state(){
	digi_safe.temp_code.set_code();  //break
	if(digi_safe.temp_code.get_active() == 1){ //Code complete 
		if(last_state == userUnlocked){
			digi_safe.update_code(0);
		}
		else if(last_state == adminUnlocked){
			digi_safe.update_code(1); 
		}
		else {
			while(1); //how? - for debugging 
		}
		back_state(); 
	}
	else if(digi_safe.temp_code.get_active() == 2){ //* to cancel 
		digi_safe.temp_code.set_active(0);
		back_state();  
	}	
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void state_machine::lockout_state(){
	for(int8_t i =0; i<LOCKTIME; i++){
		err(); 
	}
	set_state(last_state); 
}

//*****************************************************************************************************************************************************************//
// OTHER CLASS FUNCTIONS
//*****************************************************************************************************************************************************************//

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void lock::update_code(lock * new_code, int8_t pos){
	unsigned int point = (pos*4)+4, search=24; //create pointer to write location according to header
	EEPROM_write(point, 1);  //set code to valid in eeprom
	point++;
	code_length = new_code->get_code_len(); //update length
	EEPROM_write(point, code_length);  //in EEPROM
	point++;
	int8_t find = 1; //variable will be used to control while loop for finding valid locations
	volatile unsigned char test;
	while(find){
		test = EEPROM_read(search);
		if(test == 0xCC){
			search=search+4;
		}
		else{
			find = 0;
			EEPROM_write(point,(int8_t)((search>>8)&0xFF));
			point++;
			EEPROM_write(point,(int8_t)(search&0xFF));
			point = search;
			EEPROM_write(point,0xCC);
			point++;
			code[0] = new_code->get_code_digit(0);
			EEPROM_write(point, code[0]);
			point++;
		}
	}
	search = search+4;
	for(uint8_t i=1; i< code_length; i++){
		code[i] = new_code->get_code_digit(i);
		find = 1;
		while(find){
			test = EEPROM_read(search);
			if(test == 0xCC){
				search=search+4;
			}
			else{
				find = 0;
				EEPROM_write(point,(int8_t)((search>>8)&0xFF));
				point++;
				EEPROM_write(point,(int8_t)(search&0xFF));
				point = search;
				EEPROM_write(point,0xCC);
				point++;
				EEPROM_write(point, code[i]);
				point++;
			}
		}
		search = search+4;
	}
	new_code->set_active(0);
	active = 1;
}

//*****************************************************************************************//
// set code
// Class: lock 
// fills the code member array
// sets active to 1 when finished 
// sets active to 2 when cancel is called 
// Inputs: key from user 
//*****************************************************************************************//
void lock::set_code(){
	int8_t key = format(); 
	//int8_t key = simKey();
	if(key != -1){
		if(key == 10){
			set_active(2);
		}
		else if(key == 11){ //if key is #
			if(code_length >= MINLEN){
				set_active(1);
			}
			else{
				err();
			}
		}
		else if(key >= 12){ //if key is a letter
			err();
		}
		else if(key >= 0 && key < 10){ //if key is a number
			if(code_length < MAXLEN){
				//add to code
				code[code_length] = key;
				code_length++;
			}
			else {
				err();
			}
		}
	}
}

//*****************************************************************************************************************************************************************//
// FUNCTIONS 
//*****************************************************************************************************************************************************************//

//*****************************************************************************************//
// get Key
// Gets a key press from keypad and returns key
// Outputs: -1 for no key or key value
//*****************************************************************************************//
int8_t getKey(){	//reads key - same as part B
	unsigned char keys[4][5] = {{0xEF,0xEE,0xED,0xEB,0xE7},{0xDF,0xDE,0xDD,0xDB,0xD7},{0xBF,0xBE,0xBD,0xBB,0xB7},{0x7F,0x7E,0x7D,0x7B,0x77}};
	int8_t values[4][4] = {{1,4,7,10},{2,5,8,0},{3,6,9,11},{12,13,14,15}};
	int8_t key;
	for(int8_t i = 0; i<4; i++){
		PORTC = keys[i][0];
		delay(10);
		key = PINC;
		if(key != (int8_t)keys[i][0]){
			int8_t val = PORTB; 
			val &= ~0x0F; 
			PORTB = val; 
			releaseKey(key); //waits for key release to ensure key is read once
			for(int8_t j=1; j<5; j++){
				if(key == (int8_t)keys[i][j]){
					int8_t n = values[i][j-1];
					if(n!=10){
						val |=  n;
					}
					PORTB = val;
					return n;
				}
			}
		}
	}
	return -1;
}

//*****************************************************************************************//
// Sim Key
// returns pin c value for easier simulation
// Outputs: Key from pin C
//*****************************************************************************************//
int8_t simKey(){
	int8_t val = PORTC;
	if(val <= 15){
		return val;
	}
	else {
		return -1;
	}
}

//*****************************************************************************************//
// release key
// waits for key to be released
// Inputs: Key
//*****************************************************************************************//
void releaseKey(int8_t key){
	int8_t x = PINC;  //waits for key release by polling pin C
	while(x == key){
		x = PINC;
	}
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void delay(int ms){
	volatile int a, j;
	j = 545;
	for(int i=0; i<ms; i++){
		a = 0;
		while(a<j){
			a++;
		}
	}
}

//*****************************************************************************************//
// err
// Error function that flashes LEDs 
//*****************************************************************************************//
void err(){
	int8_t temp = PORTB;
	for(int8_t i=0; i<3; i++){
		PORTB = 0xFF;
		delay(200);
		PORTB = 0x00;
		delay(200);
	}
	temp &= ~0x0F; 
	PORTB = temp;
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
void EEPROM_write(unsigned int uiAddress, unsigned char ucData)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEWE))
	;
	/* Set up address and data registers */
	EEAR = uiAddress;
	EEDR = ucData;
	/* Write logical one to EEMWE */
	EECR |= (1<<EEMWE);
	/* Start eeprom write by setting EEWE */
	EECR |= (1<<EEWE);
}

//*****************************************************************************************//
//
//
//
//*****************************************************************************************//
unsigned char EEPROM_read(unsigned int uiAddress)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEWE))
	;
	/* Set up address register */
	EEAR = uiAddress;
	/* Start eeprom read by writing EERE */
	EECR |= (1<<EERE);
	/* Return data from data register */
	return EEDR;
}

