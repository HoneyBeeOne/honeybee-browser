# تنسيق كود C++

نستخدم [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html) لتوحيد
تنسيق كود C++. يقرأ البرنامج الإعدادات من ملف `.clang-format` الموجود في جذر
المستودع.

قبل إرسال التغييرات، شغّل `clang-format -i` على ملفات C++ التي عدّلتها. مثلًا:

```bash
clang-format -i src/main.cpp src/browser/app.cpp src/browser/app.hpp src/browser/window.cpp src/browser/window.hpp
```

يعدّل الخيار `-i` الملفات مباشرةً. راجع التغييرات بعد تشغيله قبل عمل commit.
