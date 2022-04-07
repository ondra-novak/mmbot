/*
 * errhandler.h
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ERRHANDLER_H_
#define SRC_MAIN_ERRHANDLER_H_

///Report unhandled exception
void report_unhandled(const char *file, int line);


#define REPORT_UNHANDLED() report_unhandled(__FILE__,__LINE__)




#endif /* SRC_MAIN_ERRHANDLER_H_ */
