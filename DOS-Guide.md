# DOS 1.0 Guide

**DOS**는 Windows에서 실행되는 텍스트 기반 CLI이자 `.dos` 프로그래밍 환경이다.

`dos.exe` 하나로 명령줄 작업을 수행할 수 있고, 같은 환경에서 `.dos` 파일을 작성해 함수·구조체·배열·포인터·조건문·반복문 등을 사용하는 프로그램을 만들 수 있다.

> 이 문서는 현재 DOS 1.0 구현을 기준으로 작성되었다.

---

## 1. DOS란?

DOS는 운영체제가 아니다.

```text
Windows
  └─ dos.exe
       ├─ CLI
       ├─ .dos 실행기
       ├─ 표준 함수
       ├─ Windows 작업 명령
       └─ Python 플러그인
```

DOS의 목표는 문법을 어렵게 만들지 않으면서도 단순한 파일 작업부터 자동화와 일반적인 프로그래밍까지 한 환경에서 처리하는 것이다.

---

## 2. 실행

`dos.exe`를 실행한다.

```text
C:\> dos.exe
DOS 1.0
Type HELP for help.

C:\>
```

인자 없이 실행하면 DOS 셸이 시작되고 프롬프트가 계속 유지된다.

버전 확인:

```text
C:\> dos --version
DOS 1.0
```

도움말:

```text
C:\> dos --help
```

---

## 3. `.dos` 파일 실행

DOS 소스 파일의 확장자는 `.dos`다.

예를 들어 `hello.dos`:

```dos
println("Hello, DOS")
```

실행:

```text
C:\> dos run hello.dos
Hello, DOS
```

다음처럼 파일명을 직접 실행할 수도 있다.

```text
C:\> hello.dos
Hello, DOS
```

Windows에 파일 연결을 설치하면 바탕화면이나 탐색기에서 `.dos` 파일을 **더블클릭하여 실행**할 수 있다.

`install_association.bat`를 `dos.exe`와 같은 폴더에서 관리자 권한 없이 실행하면 현재 사용자 계정에 `.dos` 연결을 등록한다.

---

## 4. 기본 CLI 명령

### 파일과 디렉터리

```text
DIR
CD <directory>
PWD
MKDIR <directory>
RMDIR <directory>
COPY <source> <destination>
MOVE <source> <destination>
DELETE <file>
RENAME <source> <destination>
TYPE <file>
TREE [directory]
```

예:

```text
C:\Projects> dir
C:\Projects> mkdir build
C:\Projects> cd build
C:\Projects\build> pwd
C:\Projects\build> cd ..
```

`LS`, `MD`, `RD`, `DEL`, `RM`, `REN`, `CAT` 같은 별칭도 제공된다.

### 콘솔

```text
CLS
CLEAR
ECHO Hello
```

### 프로세스

```text
PROCESS LIST
PROCESS KILL <pid>
```

### 네트워크

```text
NETWORK PING example.com
```

### 시스템 정보

```text
SYSTEM INFO
```

### 외부 프로그램

DOS는 Windows의 외부 명령도 실행할 수 있다.

```text
C:\> notepad.exe
C:\> compiler.exe main.cpp -o app.exe
```

지원하지 않는 명령도 Windows `cmd.exe`로 전달된다.

---

## 5. 명령줄 기능

### 환경 변수

```text
C:\> set NAME=DOS
C:\> echo %NAME%
DOS
```

`%NAME%` 형식으로 환경 변수를 치환할 수 있다.

### 인용부호

경로에 공백이 있으면 따옴표를 사용한다.

```text
C:\> cd "C:\Program Files"
```

### 종료

```text
C:\> exit
```

종료 코드를 지정할 수도 있다.

```text
C:\> exit 1
```

---

## 6. `.dos` 언어 기본 문법

DOS의 블록은 `{`와 `}`를 사용한다.

```dos
if value > 10 {
    println("large")
} else {
    println("small")
}
```

`end` 키워드는 사용하지 않는다.

문장은 줄바꿈으로 구분할 수 있으며 세미콜론도 사용할 수 있다.

```dos
int a = 10
int b = 20

int c = a + b
```

또는:

```dos
int a = 10;
int b = 20;
int c = a + b;
```

---

## 7. 주석

한 줄 주석:

```dos
// comment
```

블록 주석:

```dos
/*
   comment
   comment
*/
```

---

## 8. 자료형

DOS는 C 계열의 타입 표기를 제공한다.

```text
i8    i16    i32    i64
u8    u16    u32    u64
isize usize
float f32 f64 double
int
bool
char
byte
string
void
ptr
```

예:

```dos
int count = 10
string name = "DOS"
bool enabled = true
float ratio = 3.14
```

`var`를 사용하면 초기값에서 타입을 정한다.

```dos
var count = 10
var name = "DOS"
```

---

## 9. 문자열

```dos
string name = "DOS"
println(name)
```

문자열 연결:

```dos
string text = "Hello, " + "DOS"
println(text)
```

지원되는 기본 escape:

```text
\n
\r
\t
\\
\"
```

---

## 10. 배열

배열 리터럴:

```dos
int[] numbers = [10, 20, 30]
```

접근:

```dos
println(numbers[0])
numbers[1] = 50
```

크기 확인:

```dos
println(len(numbers))
```

---

## 11. 구조체

구조체는 `{ }` 안에 필드를 정의한다.

```dos
struct User {
    string name
    int age
}
```

사용:

```dos
User user
user.name = "DOS"
user.age = 1

println(user.name)
```

---

## 12. 함수

DOS는 C 스타일 함수 선언을 지원한다.

```dos
int add(int a, int b) {
    return a + b
}
```

호출:

```dos
int result = add(10, 20)
println(result)
```

`function` 키워드를 사용하는 형태도 지원한다.

```dos
function add(int a, int b) {
    return a + b
}
```

반환값이 없는 함수:

```dos
function hello() {
    println("Hello")
}
```

---

## 13. 조건문

```dos
if value == 10 {
    println("ten")
} else if value > 10 {
    println("large")
} else {
    println("small")
}
```

논리 연산:

```dos
&&
||
!
```

DOS는 `&&`와 `||`에서 short-circuit evaluation을 사용한다.

---

## 14. 반복문

### `while`

```dos
int i = 0
while i < 3 {
    println(i)
    i++
}
```

### C 스타일 `for`

```dos
for int i = 0; i < 3; i++ {
    println(i)
}
```

### 배열 순회

```dos
int[] values = [10, 20, 30]

for int value in values {
    println(value)
}
```

중단:

```dos
break
```

다음 반복으로 이동:

```dos
continue
```

---

## 15. `switch`

```dos
switch value {
    case 1:
        println("one")
        break
    case 2:
        println("two")
        break
    default:
        println("other")
}
```

---

## 16. 포인터

DOS는 포인터 문법을 지원한다.

```dos
int value = 100
int* ptr = &value

*ptr = 200

println(value)
```

`ptr<T>` 형식도 사용할 수 있다.

```dos
ptr<int> p
```

널 포인터는 `null`로 표현한다.

```dos
ptr<int> p = null
```

잘못된 역참조는 DOS 오류로 처리된다.

---

## 17. 연산자

### 산술

```text
+
-
*
/
%
```

### 비교

```text
==
!=
<
<=
>
>=
```

### 논리

```text
&&
||
!
```

### 비트

```text
&
|
^
~
<<
>>
```

### 대입

```text
=
+=
-=
*=
/=
%=
```

### 증감

```text
++
--
```

### 삼항

```dos
string text = value > 10 ? "large" : "small"
```

---

## 18. 오류 처리

예외 처리에는 `try`, `catch`, `throw`를 사용한다.

```dos
try {
    throw "something failed"
} catch error {
    println(error)
}
```

`assert`도 사용할 수 있다.

```dos
assert(value == 10)
```

메시지를 지정할 수 있다.

```dos
assert(value == 10, "value must be 10")
```

문법 오류에는 줄과 열 정보가 표시된다.

```text
DOS error at 4:9: expected }
```

---

## 19. 표준 함수

### 출력

```dos
print("Hello")
println("Hello")
echo("Hello")
```

### 입력

```dos
string line = read_line()
```

### 형 변환

```dos
string(123)
int("123")
float("3.14")
bool(1)
```

### 기본 정보

```dos
cwd()
pwd()
len(value)
exit_code()
sleep(100)
```

---

## 20. 파일 시스템 API

### 읽기

```dos
string text = fs.read("test.txt")
```

### 쓰기

```dos
fs.write("test.txt", "Hello DOS")
```

### 추가

```dos
fs.append("test.txt", "\nNext line")
```

### 디렉터리

```dos
fs.mkdir("build")
fs.rmdir("build")
```

### 파일 복사/이동/삭제

```dos
fs.copy("a.txt", "b.txt")
fs.move("b.txt", "c.txt")
fs.remove("c.txt")
```

### 검색

```dos
var files = fs.find(".", "*.cpp", true)
```

### 목록

```dos
var entries = fs.list(".")
```

목록의 항목은 다음 값을 제공한다.

```text
name
path
directory
file
size
```

---

## 21. 환경 변수 API

```dos
string path = env.get("PATH")
env.set("DOS_NAME", "DOS")
```

CLI에서도 같은 환경을 사용할 수 있다.

```text
C:\> set DOS_NAME=DOS
C:\> echo %DOS_NAME%
DOS
```

---

## 22. 문자열 API

```dos
string.upper("hello")
string.lower("HELLO")
string.contains("Hello DOS", "DOS")
string.substr("Hello DOS", 0, 5)
```

예:

```dos
var text = "Hello DOS"
println(string.upper(text))
println(string.contains(text, "DOS"))
```

---

## 23. 수학 API

```dos
math.abs(-10)
math.sqrt(16)
math.pow(2, 8)
math.min(10, 20)
math.max(10, 20)
```

---

## 24. 프로세스 API

현재 DOS 프로세스 ID:

```dos
int pid = process.id()
println(pid)
```

프로그램 실행:

```dos
int code = process.run("notepad.exe")
```

프로세스 종료:

```dos
bool result = process.kill(pid)
```

CLI에서도:

```text
process list
process kill 1234
```

---

## 25. 네트워크 API

```dos
bool online = network.ping("example.com")
println(online)
```

CLI:

```text
network ping example.com
```

현재 구현의 ping은 Windows `ping` 명령을 이용한다.

---

## 26. 시스템 API

```dos
system("dir")
```

Windows 시스템 정보:

```text
system info
```

실행 중인 외부 명령의 종료 코드는 다음과 같이 가져올 수 있다.

```dos
int code = exit_code()
```

---

## 27. Python 플러그인

DOS는 Python을 **플러그인 방식**으로 사용한다.

플러그인 구조:

```text
plugins/
└── python/
    ├── plugin.dosplugin
    └── plugin.py
```

Python 자체를 DOS 언어에 복사해 넣는 것이 아니라, 설치된 Python 3 인터프리터를 DOS 플러그인이 호출한다.

Python 실행 파일을 자동으로 찾으며, 필요한 경우 `DOS_PYTHON` 환경 변수로 직접 지정할 수 있다.

### CLI

Python 파일 실행:

```text
C:\> dos
DOS> python hello.py
```

직접 코드 실행:

```text
DOS> python exec "print('Hello from Python')"
```

버전 확인:

```text
DOS> python --version
```

플러그인 목록:

```text
DOS> plugin list
```

### `.dos`에서 사용

```dos
import python

python.run("hello.py")
python.exec("print('Hello')")
python.version()
python.available()
```

---

## 28. 명령과 함수의 결합

DOS의 특징은 CLI 명령과 프로그래밍 API를 함께 사용할 수 있다는 것이다.

`.dos` 안에서 일반 명령을 그대로 작성할 수 있다.

```dos
dir
mkdir build
copy input.txt build/input.txt
```

복잡한 작업은 함수와 함께 구성할 수 있다.

```dos
function build() {
    mkdir("build")
    copy("main.dos", "build/main.dos")
    println("Build preparation complete")
}

build()
```

지원하지 않는 명령은 Windows `cmd.exe`로 전달된다.

---

## 29. 배치 자동화

`.dos`는 단순 명령 모음이 아니라 조건·반복·함수 등을 포함한 DOS 프로그램으로 사용할 수 있다.

예:

```dos
string[] files = fs.find("src", "*.cpp", true)

for string file in files {
    println("Found: " + file)
}

if len(files) == 0 {
    println("No source files")
} else {
    println("Source files: " + string(len(files)))
}
```

---

## 30. 프로그램 패키징

`dos.exe`는 `.dos` 소스를 PE 실행 파일에 내장하는 패키징 기능을 제공한다.

기본:

```text
C:\> dos build hello.dos -o hello.exe
```

특정 PE 기반 이미지를 지정하려면:

```text
C:\> dos pe-build hello.dos -b base.exe -o hello.exe
```

패키징된 프로그램은 실행 시 내부에 저장된 `.dos` 소스를 찾아 실행한다.

패키지 검증:

```text
C:\> dos verify pe hello.exe
```

DOS 패키지까지 확인:

```text
C:\> dos verify package hello.exe
```

패키지에는 DOSPKG4 메타데이터와 CRC32가 사용된다. 현재 구현은 DOSPKG3 형식 읽기도 지원한다.

---

## 31. `.dos` 더블클릭 실행

Windows 파일 연결을 설치한 뒤:

```text
Desktop
└── hello.dos
```

파일을 더블클릭하면 등록된 `dos.exe`가 다음과 같이 실행한다.

```text
hello.dos
   ↓
dos.exe "hello.dos"
   ↓
DOS 프로그램 실행
```

`.dos`를 직접 더블클릭하여 실행한 경우 DOS는 콘솔 창이 바로 닫히지 않도록 종료 전에 대기한다.

```text
Press Enter to close...
```

---

## 32. 완전한 예제

다음 프로그램은 구조체, 함수, 배열, 반복문, 조건문, 파일 API를 함께 사용하는 예제다.

```dos
struct FileInfo {
    string name
    int size
}

function print_file(string path) {
    if fs.exists(path) {
        string text = fs.read(path)
        println(text)
    } else {
        println("File not found: " + path)
    }
}

string[] files = fs.find(".", "*.txt", true)

for string file in files {
    println("File: " + file)
}

if len(files) > 0 {
    print_file(files[0])
} else {
    println("No text files")
}
```

---

## 33. 권장 프로젝트 구조

작은 프로젝트:

```text
my-project/
├── main.dos
└── README.md
```

Python 플러그인을 사용하는 프로젝트:

```text
my-project/
├── main.dos
├── scripts/
│   └── helper.py
└── README.md
```

배포 프로그램:

```text
my-project/
├── main.dos
└── main.exe
```

---

## 34. 빠른 시작

### 1. DOS 실행

```text
C:\> dos.exe
```

### 2. 파일 생성

`hello.dos`:

```dos
println("Hello, DOS!")
```

### 3. 실행

```text
C:\> dos run hello.dos
Hello, DOS!
```

### 4. 프로그래밍

```dos
int add(int a, int b) {
    return a + b
}

int value = add(10, 20)

if value > 20 {
    println("value = " + string(value))
}
```

### 5. Windows 프로그램 실행

```text
DOS> notepad.exe
```

### 6. Python 사용

```text
DOS> python hello.py
```

---

## 35. 명령 요약

```text
help
ver
version
cls
clear
pwd
cd
 dir
ls
tree
mkdir
md
rmdir
rd
copy
move
delete
del
rm
rename
ren
type
cat
echo
print
set
run
start
process list
process kill <pid>
network ping <host>
system info
python <script.py>
python exec <code>
python --version
plugin list
exit
quit
```

---

## 36. 핵심 요약

```text
DOS
 ├─ Windows 전용
 ├─ 텍스트 기반 CLI
 ├─ dos.exe
 ├─ .dos 소스
 ├─ { } 블록
 ├─ C 스타일 함수/구조체/포인터
 ├─ 배열/조건/반복/switch
 ├─ 파일 시스템 API
 ├─ 프로세스 API
 ├─ 네트워크 API
 ├─ 환경 변수
 ├─ Windows 명령 연동
 ├─ Python 플러그인
 └─ .dos → PE 패키징
```

**DOS는 짧은 CLI 명령으로 쉬운 작업을 처리하면서, 동일한 `.dos` 환경에서 더 복잡한 자동화와 프로그램을 작성할 수 있도록 설계된 Windows용 CLI다.**
