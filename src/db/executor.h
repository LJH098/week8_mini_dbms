#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "executor_result.h"
#include "parser.h"

/*
 * 파싱이 끝난 SQL 문 하나를 실행하고 CLI 형태로 출력한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int executor_execute(const SqlStatement *statement);

/*
 * 파싱된 SQL 문을 실행해 구조화된 DbResult를 채운다.
 */
int executor_execute_into_result(const SqlStatement *statement, DbResult *out_result);

/*
 * DbResult를 기존 CLI와 같은 형태로 렌더링한다.
 */
void executor_render_result_for_cli(const DbResult *result);

#endif
