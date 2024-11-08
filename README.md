# CSED312_OS

id : stu45

pw : 운영45

+ 개발서버 접속 방법
1. shell 접속
2. $ ssh stu45@141.223.108.159
3. $ 운영45

# manual
1. standford (eng)
https://web.stanford.edu/class/cs140/projects/pintos/pintos.html#SEC_Top

2. standford (kor)
https://github.com/yuhodots/pintos/blob/master/manual_kor/Project%200.%20Manual.md

3. kaist version (kor)
https://yjohdev.notion.site/Introduction-8f2bad2835c641488a6ab09613911eb1

+ kaist version(64bit)은 기존 standford version(32bit)에서 수정되었으나, 기본 개념 설명은 아주 잘 되어있음.

# setting
0. install docker for macOS
1. ./make_container.sh
2. Visual studio code
install dev containers, remote explorer
3. Find pintos container
4. attach to VS code
5. drag and drop 3 files (set_env.sh, download_pintos.sh, set_pintos.sh)
6. chmod +x set_env.sh download_pintos.sh set_pintos.sh
7. ./set_env.sh
8. ./download_pintos.sh
9. Restart terminal
10. ./set_pintos.sh
11. 이후, vscode extension으로 한다면 조교님들이 알려주신 방법이다. /threads 폴더에서 make check시 20 of 27 tests failed. 뜨면 setup 완료.

+ 이후, 로컬 폴더와 싱크 맞추는 법 : https://pkuflyingpig.gitbook.io/pintos/getting-started/environment-setup
0. git init을 통해 remote와 local 저장소 연결
1. VScode를 통해 container 내부에 있는 pintos 폴더 다운로드.
local 저장소에 'pintos 폴더 안의 내용물'을 저장.
이때, pintos 폴더 자체를 넣어버리면 .sh에 적어둔 PATH가 꼬이니까, pintos폴더 내의 내용물만 복붙.
2. docker에서 container ID 확인
3. sudo docker commit -p [컨테이너 ID] [저장할 이미지 이름]
4. sudo docker images로 이미지가 잘 저장되었는지 확인
5. local 저장소와 docker container 내부의 pintos 폴더의 싱크를 맞추는 방법
docker run -it --rm --name pintos --mount type=bind,source=/Users/{사용자명}/{local저장소}},target=/home/pintos {저장한 이미지 이름}}
6. make check로 올바르게 돌아가는지 확인
7. local에서 123.txt 파일 생성하여 싱크 맞춰지는지 확인
8. sourcetree 등 git application을 통해 편하게 사용할 수 있다.

+ docker run -it --rm --name pintos --mount type=bind,source=/Users/{유저이름}/sourcetree/CSED312_OS,target=/home/pintos csed312_os
+ /threads 폴더에서 make check시 20 of 27 tests failed. 뜨면 setup 완료.

# 개별 테스트 돌리는 법
reference : https://pintosiiith.wordpress.com/2012/10/01/running-test-cases-for-pintos-assignment/

예시 : 

$ make clean

$ make tests/threads/alarm-multiple.result

이는 project 1 : thread의 alarm-multiple test를 개별적으로 실행한다.

1.파일 경로를 직접 설정해도 되고, 

2.make check 했을 때 나오는 명령어로 확인할 수 있다.

# 디버깅 방법 : gdb
reference : https://www.notion.so/pintos-gdb-a6e98e8768324a46905e954fbb387da4

원문의 pintos-kaist notion이 닫힌 것 같아, 이전에 복사해두었던 주소가 남아있어 첨부.