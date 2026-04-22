# Gemini PR Review

이 저장소에는 GitHub Actions 기반 Gemini PR 리뷰 워크플로우가 설정되어 있습니다.

## 필요한 설정

GitHub 저장소 Settings > Secrets and variables > Actions에서 아래 secret을 추가해야 합니다.

- `GEMINI_API_KEY`: Google AI Studio에서 발급한 Gemini API 키

선택 설정:

- `GEMINI_MODEL`: 사용할 Gemini 모델 이름
- `GEMINI_CLI_VERSION`: Gemini CLI 버전 고정
- `APP_ID`, `APP_PRIVATE_KEY`: 커스텀 GitHub App 인증을 쓸 때

그리고 GitHub 저장소 Settings > Actions > General에서
`Workflow permissions`를 `Read and write permissions`로 설정해야
PR 리뷰 코멘트를 실제로 작성할 수 있습니다.

## 자동 리뷰

아래 경우에 자동으로 리뷰가 실행됩니다.

- 같은 저장소 브랜치에서 생성된 PR이 `opened` 또는 `reopened` 될 때

기본 설정에서는 fork PR은 자동 실행하지 않습니다.

## 수동 리뷰

PR 코멘트에 아래처럼 남기면 수동 리뷰를 다시 실행할 수 있습니다.

```text
@gemini-cli /review
```

추가 포커스를 줄 수도 있습니다.

```text
@gemini-cli /review focus on security and benchmark correctness
```

## 수동 실행

GitHub Actions 탭에서 `Gemini PR Review` 워크플로우를 직접 실행할 수도 있습니다.

- `pr_number`: 리뷰할 PR 번호
- `additional_context`: 추가 리뷰 지시사항
