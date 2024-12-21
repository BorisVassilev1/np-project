# Проект по Мрежово Програмиране - Многонишков HTTP сървър

## Архитектура
Проектът реализира многонишков TCP сървър, както и имплементация, която работи с HTTP пакети. Заявки от различни клиенти могат да бъдат обработвани паралелно, при наличие на достатъчен брой нишки. Заявки от един и същи клиент винаги се обработват последователно. Всяка отделна нишка сама играе ролята на сървър и може да приема нови връзки и да преустановява такива.

Във файла `main.cpp` e показан пример за използването на абстрактния HTTP сървър. Така създаденият сървър оговаря на заявки:
- `/` - страница, позволяваща въвеждането на числа и изпращането им до сървъра за сортиране. (показва съдържанието на пакпката `/public`)
- `/wait` - тази заявка приспива изпълняващата я нишка за няколко секунди и отговаря с просто съобщение
- `/dir/` - показва съдържанието на директорията, в която е пуснат сървъра
- `/asd` - тази заявка винаги връща статус 500.
- `/sort` - на този адрес се подават заявки за сортиране на числа.

## Използвани технологии
Проектът се компилира под стандарта c++20 и използва Linux системни извиквания за работа със сокети и файлове.

## Инструкции за компилация
Проектът се компилира с gcc/clang и cmake. Най-просто се компилира така:
```sh
./configure.sh
cmake --build ./build/ -j8
```
Проектът компилира два изпълними файла: `server` и `client`.

`server` реализира основната функционалност на проекта - приема аргумент брой нишки, на които да се изпълнява, както и порт и слуша за заявки на `[::1]:<port>`. При липса на аргументи, сървърът се изпълнява на максималния брой нишки, които системата позволява да се изпълняват конкурентно и използва порт `8080`.
`client` може да се използва за демонстриране на конкурентността на сървъра. Клиентът може да имитира няколко отделни клиента и да прати по няколко заявки от всеки от тях и да очаква отговор за всяка изпратена заявка. При изпълняване на командата без аргументи, може да се види по-подробно описание на заявките, които могат да бъдат изпратени. Клиентът винаги праща заявки на `[::1]:8080` (настройките по подразбиране на сървъра).
