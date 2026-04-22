#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/*
 * 파싱이 끝난 SQL 문 하나를 실행한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int executor_execute(const SqlStatement *statement);

#endif
