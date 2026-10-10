# 📋 SeedLens — تقرير Audit مستقل

- **التاريخ:** 2026-10-06
- **المدقّق:** Buffy (Freebuff) — Audit مستقل، بدون تعديل أي ملف في المشروع
- **نطاق الفحص:** `engine/`, `cli/`, `tests/`, `docs/`, `README.md`, `Makefile`, `frontend/`, وعلاقتها بـ `/home/pc/Downloads/cubiomes` (HEAD = `e61f905`).
- **التزام بالقواعد:** لم يُعدل أي ملف في المشروع (تم التحقق عبر git — المشروع ليس أصلاً مستودع git). تجارب التحقق كلها في `/tmp` (`audit_check.c`, `audit_engine*`, `seedlens_asan`). أُعيد بناء الـ artifacts فقط (`make`) — وكلها غير متتبعة.

**منهجية:** قراءة كاملة للكود + تشغيل فعلي (regression tests, ASAN/UBSan, 35 seed × تشغيلين للتحقق من الحتمية) + برامج تحقق مستقلة ضد Cubiomes مباشرة (spawn، terrain false positives، سلوك سقف العناقيد).

---

## Executive Summary

**ما حالة المحرك؟**

النواة (`engine/`) مكتوبة بشكل جيد لمرحلة prototype: C نظيف، pipeline واضح (`discovery → cluster → analysis → explanation → JSON`)، حتمي بالكامل (تحققت: 35 seed × تشغيلين → نتائج متطابقة)، سريع (~0.08–0.11 ث/seed، RSS ~4–5MB)، ونظيف تحت ASAN/UBSan. اختبارات الـ regression الخمسة تمر كلها. حسابات الـ region iteration صحيحة رياضياً لكل الأنواع الخمسة عند ±5000 (تحققت يدوياً من الـ offsets القصوى لكل config).

**لكن المحرك بوضعه الحالي ليس جاهزاً كمنتج حقيقي**، لخمس أسباب جوهرية أثبتها القياس العملي:

1. **`nearest_spawn` ليس spawn**: إنه المسافة إلى `(0,0)` — لم يُستدعِ المحرك `getSpawn` أبداً. فئة `Spawn Outpost` كاملة مبنية على قياس خاطئ، والشرح يقول حرفياً "blocks from world spawn" (`explanation.c:80`).
2. **سقف 20 عنقود يُطبَّق قبل فلترة الـ usefulness فيحذف نتائج useful فعلاً**: أثبتّه قياسياً — على الـ seed المرجعي نفسه `123456789` يوجد 5 عناقيد useful والمحرك يُخرج 4 فقط. على عينة 20 seed عشوائي: متوسط useful = 6.15 بالسقف الحالي مقابل 8.60 بدون السقف، و**18 من 20 seed يخسرون عناقيد useful بصمت**.
3. **False positives موثقة القياس لعدم استخدام `isViableStructureTerrain`**: ~29% من الأهرامات الصحراوية/معابد الغابة التي يعدّها المحرك "viable" تفحص الـ terrain-check التقريبي وتفشل (مثال: seed 42 → 16 من 30 هرماً صحراوياً).
4. **طبقات الإدخال غير متسقة**: الـ CLI يقبل seed خارج نطاق int64 بصمت ويُسقِطه على `INT64_MAX` (جرّبت `99999999999999999999` → حُلّل كـ `9223372036854775807` و exit 0) — أي يحلل **عالماً خاطئاً**؛ بينما الـ frontend يرفضه، وMinecraft نفسها تعامله كنص وتعمل hash.
5. **الواجهة تبالغ في الادعاء**: الـ frontend تعرض خريطة "تضاريس" مولّدة بـ Simplex noise مخصص (`minecraftTerrain.ts`) لا علاقة له بالعالم الحقيقي، وتقول في صفحة الشرح "بدقة 100%"، وتسمّي `(0,0)` بـ "World Spawn". المحرك نفسه لا يفرّق بين candidate/viable/confirmed إطلاقاً.

إضافة عملية حرجة: **المشروع ليس مستودع git**، وCubiomes غير مثبّت الإصدار (unpinned) بمسارات مطلقة خاصة بالجهاز، والـ binary يرتبط ديناميكياً بـ `libcubiomes.so` داخل `~/Downloads`.

---

## Critical Problems

**C1 — مقياس "Spawn" خاطئ أصلاً (دقة/نزاهة المخرجات)**
- الدليل: `discovery.c:72` يحسب `distance = sqrt(x²+z²)` من الأصل؛ `cluster.c` يسمّيه `nearest_spawn`؛ `analysis.c` يستخدمه كبوابة `Spawn Outpost ≤ 1000`؛ `explanation.c:80` يقول "from world spawn"; الـ UI (`translations.ts`: `mapSpawnMarker: 'World Spawn (0, 0)'`) يرسم `(0,0)` كـ spawn.
- قياساً (عينة 10 seeds): الـ spawn الحقيقي (`getSpawn`) يبعد عن الأصل 0–462 بلوك. انحراف حتى ~500 بلوك مقابل عتبة 1000 = تحويل منهجي لـ false positives/negatives لفئة كاملة.
- التأثير: كل نتيجة `Spawn Outpost` غير موثوقة، والـ explanation مضلل.
- ملاحظة: `getSpawn` بطيء وغير دقيق في عوالم المحيطات (توثيق Cubiomes صراحةً) — البديل الأدنى: إعادة تسمية الحقل إلى `distance_to_origin` مع إفصاح صريح.

**C2 — سقف 20 عنقود يُطبَّق قبل تقييم الـ usefulness → حذف صامت لنتائج**
- الدليل: `cluster.c:210` (`final_count < SEEDLENS_MAX_CLUSTERS`) يقتطع top-20 بترتيب `cluster_better` (type_count > count > nearest_spawn) **ثم** `engine.c` يفلتر useful لاحقاً. `summary.total_clusters` = العدد بعد السقف، بلا أي flag يوضح الاقتطاع.
- قياساً: السقف فعّال في 6/6 seeds المختبرة؛ useful المحذوفة: `123456789`: 4/5 — `-1467692750553100038`: 8/11 — `1844674407370955161`: 6/10 — `739208174635920481`: 4/5 — `202620262026202626`: 9/15 — `42`: 9/12. وعلى 20 seed عشوائي: 18/20 تخسر عناقيد useful.
- ملاحظة مميتة للاختبارات: الـ regression test يؤكد `useful_clusters == 4` للـ seed المرجعي — **الاختبار يجمّد الخطأ** (القيمة الصحيحة 5).

**C3 — الـ CLI يحلل عالماً خاطئاً للـ seeds خارج النطاق**
- الدليل: `cli/main.c:31` `strtoll` بلا فحص `errno/ERANGE`. `99999999999999999999` → يُسقَط إلى `9223372036854775807` ويُحلَّل بنجاح (exit 0). أما Minecraft فتعامل النص الرقمي الفائض كـ **نص** وتعمل `String.hashCode()`.
- التعارض الطبقي: الـ frontend (`validation.ts`) يرفض خارج int64 عبر BigInt (سلوك معقول لكنه مختلف)، الـ CLI يُسقِط (سلوك خاطئ)، Minecraft تعمل hash (سلوك ثالث).

**C4 — لا يوجد Version Control + تبعية Cubiomes غير مثبتة بمسارات خاصة بالجهاز**
- `SeedLens` ليس مستودع git (تحقق: `git rev-parse` يفشل). لا تاريخ، لا rollback، لا إمكانية مقارنة تكرارات لاحقة.
- `Makefile` يشير إلى `/home/pc/Downloads/cubiomes` (مطلق، غير submodule/commit-pinned)، والـ binary النهائي مرتبط **ديناميكياً**: `ldd` → `libcubiomes.so => /home/pc/Downloads/cubiomes/build/libcubiomes.so`. نقل/تنظيف مجلد Downloads يكسر الـ binary.

**C5 — لا فرق بين candidate/viable/confirmed + ادعاءات واجهة مضللة**
- المحرك يمرر كل شيء عبر بوابة واحدة (`isViableStructurePos` = فحص biome فقط لمعظم الأنواع) ويُخرجها كـ structures نهائية بلا أي حقل ثقة/مستوى إثبات في الـ JSON.
- `isViableStructureTerrain` **لا يُستدعى إطلاقاً** رغم توفره في Cubiomes للأهرامات/المعابد.
- الـ frontend: `step1Desc` = "…بدقة 100%" (عربية) و"deterministically simulate … terrain" (إنجليزية) — غير صحيح: Cubiomes لا يحاكي terrain، وخريطة الـ UI مولّدة من `MinecraftTerrainEngine` (Simplex noise مخصص بـ hash 32-bit من نص الـ seed) — أي **خلفية خرائط وهمية** تُرسم عليها structures حقيقية.

---

## High Priority

**H1 — False positives الأهرامات/المعابد (قياسي)**
باستخدام `isViableStructureTerrain` التقريبي على مواقع المحرك نفسها: Desert Pyramid: 39 فشل من 108 viable (~36%)؛ Jungle: 18 من 88 (~20%)؛ الإجمالي 57/196 (~29%). هذه الأرقام تدخل مباشرة في `count`, `density`, `type_count` للعناقيد → قد تقلب تصنيف الفئات. (تأثيرها النهائي على الفئات: Needs empirical validation — يتطلب إعادة تشغيل الـ pipeline كاملة مع الفلتر).

**H2 — دقة رقمية للـ seed في سلسلة JSON/JS**
الـ JSON يُخرج seed كرقم `%lld`؛ أي seed > 2⁵³ يفقد الدقة عند `JSON.parse` في JS (تحققت: `9223372036854775807` → `9223372036854776000`). الـ API يضيف `seed_string` الصحيح لكن الـ UI يعرض `seed` الرقمي.

**H3 — المحرك غير thread-safe**
`engine.c:45,49` و`cluster.c:173` مخازن `static` مشتركة → أي استدعاءان متزامنان لـ `seedlens_analyze` يتعارضان. مقبول فقط بنموذج process-per-request (الحالي عبر `spawn`)، ويمنع التطور لـ in-process server مستقبلاً.

**H4 — مسارات فشل الذاكرة في `json.c`**
`jbuf_ensure` يتجاهل فشل `realloc` ثم يكتب → heap overflow عند OOM؛ `jbuf_init` بلا فحص `malloc(NULL)` → segfault. احتمالية منخفضة (OOM فقط) لكنه خطأ صحيح.

**H5 — عدم دعم text seeds في الـ CLI**
الـ frontend يحوّل "Goblins" → `String.hashCode()`؛ الـ CLI يرفضها. نفس الإدخال يعطي عوالم مختلفة حسب نقطة الدخول.

**H6 — لا بيانات تشخيص في المخرجات**
لا يوجد: عدد الـ candidates قبل السقف، هل حدث اقتطاع، عدد الـ records المرفوضة بحد `MAX_STRUCTURES` (رمز صامت في `discovery.c:63` — عملياً غير قابل للبلوغ بالحدود الحالية: أسوأ حالة 3421 < 4096، لكن الكود عُمّي ومخفي).

---

## Medium Priority

- **M1** — Exit codes غير متمايزة (كل الأخطاء = 1)؛ الأخطاء نص فقط بلا codes قابلة للبرمجة.
- **M2** — جدول الـ structures مكرر مرتين (`discovery.c:targets[]` و`engine.c:target_meta[]`) → خطر انحراف؛ `inventory[5]` بسعة ثابتة = عدد الأنواع.
- **M3** — معيار الترتيب `cluster_better` (تنوع > كثافة > قرب الأصل) ليس معيار "الusefulness"؛ مع السقف 20 يحدد أي العناقيد تُعرض (هو سبب C2 في الواقع).
- **M4** — `same_cluster` غير متعدية (سلسلة A~B~C: قد تُحذف B وتُبقى C) وحد ثابت 250 للمركز؛ order-dependent لكن حتمي.
- **M5** — `density = count / π(diameter/2)²` تستخدم القطر الأقصى كقطر دائرة محيطة → overestimate للكثافة غير الخطية (upper bound موثق رياضياً، مقبول كـ proxy لكن غير موثق في docs).
- **M6** — مواقع الـ structures هي **chunk origins** (سلوك Cubiomes القياسي مثل `/locate`)؛ المسافات/الكثافة ترث هذا التكميم (حتى ~400 بلوك انحراف محتمل للمقارنة البصرية داخل اللعبة). قرار تصميمي مقبول لكن يجب توثيقه.
- **M7** — `/api/analyze` بلا rate limiting → CPU-DoS بسيط (كل طلب = عملية + ~0.1s CPU).
- **M8** — لا CI؛ `test_cli.sh` مجرد wrapper؛ لا unit tests للمحرك مباشرة.

---

## Low Priority

- `is_useful` دائماً = 1 في المخرجات (حقل زائد)؛ `primary_category` مشتوق مرتين (enum flags + مصفوفة أسماء).
- فحص الإصدار بمقارنة نصية صارمة فقط (`"1.20.6"`) — لا aliases.
- `jbuf_printf` بخط `tmp[1024]` نظري (كل الاستدعاءات الحالية صغيرة).
- `docs/architecture.md` لا يذكر سلوك السقف/الاقتطاع إطلاقاً.
- الـ frontend `types/seedlens.ts` يعرّف `seed: number | string` — يخفي مشكلة H2 بدل كشفها.

---

## Algorithm Problems

1. **ترتيب ثم اقتطاع ثم فلترة** (ترتيب خاطئ للعمليات) — هو C2. الحل المفهومي: فلترة useful أولاً، ثم ترتيب، ثم سقف للعرض فقط مع metadata.
2. **البناء حول record مركزي**: العضوية "داخل 500 من الـ record المركزي" وليست من مركز هندسي — ينتج عناقيد نجمية؛ محكومة بحسن نية ببوابة القطر ≤800 (PASS 4). السلوك مقبول لكنه يجب توثيقه لأنه يفسر "chains" (أ قريب من ب قريب من ج) التي يُقبل بعضها (A–C ≤ 800) ويُرفض البعض.
3. **الرفض الكلي للعناقيد الكبيرة**: قطر > 800 → يُحذف العنقود بأكمله بدل تقسيمه/إعادة تجميعه — قد ينتج false negatives لعناقيد "أوبست" ممتدة مفيدة (تصميم v3.1 مقصود على الأرجح — Needs empirical validation قبل أي تغيير).
4. **المقاييس الهندسية سليمة**: closest pair O(k²) دقيق، المركز = متوسط بسيط، الـ density محددة المعامل — لا أخطاء حسابية وجدتها.
5. **لا أخطاء في region iteration** (تحقق رياضي لكل config: Village 34/26، Shipwreck 24/20، Ancient City 24/16، Desert/Jungle 32/24 — كلها مطابقة لـ vanilla 1.18+ والـ buffer +2 كافٍ).
6. **الكشف المزدوج مستحيل بنيوياً** (كل region يولّد موقعاً واحداً لكل نوع) ✓ — لا duplicates.

---

## Architecture Problems

- **حدود الوحدات جيدة**: `discovery/cluster/analysis/explanation/engine/json` فصل صحيح، والاعتماد على Cubiomes معزول في `discovery.c` + `internal.h`. نقطة قوة فعلية.
- **تسريب enums من Cubiomes** (`Village`, `Ancient_City`…) في `analysis.c` و`engine.c` — مقبول الآن، يصبح عبئاً عند تعدد الإصدارات.
- **تعدد مصادر الحقيقة**: قائمة الأنواع ×2 (M2)، الإصدار ×4 (engine.c, cli, frontend validation, translations).
- **عمران الإصدارات (سؤال 12)**: لا يوجد أي abstraction للإصدار — إضافة 1.21 تتطلب: enum في engine.c، فلترة targets حسب الإصدار (Trial Chambers غير موجودة في 1.20.6 لكن الكود لا يعرف ذلك)، تحديث 3 طبقات أخرى، وتحديث الاختبارات. **Abstraction الموصى به**: `VersionProfile` — جدول لكل إصدار: {mc enum, قائمة structures المتاحة, configs مرجعية من getStructureConfig, categories المتاحة} — طبقتا clustering/scoring يعتمدان الهندسة فقط ويمكن إبقاؤهما version-agnostic كما هما.
- **عدم وجود VCS + تبعية غير مثبتة** (C4) — أخطر مشكلة "معمارية" عملياً: لا يمكن حتى إثبات ما يتغير بين المراجعات.
- **الخلاصة لسؤال "هل المعمارية تصلح لمنتج حقيقي؟"**: الأساس (محرك C حتمي + CLI بـ JSON + API layer) صحيح وقيّم؛ تُمنع من الإنتاج بـ: C1–C5 + عدم thread-safety (H3) + غياب مصدر حقيقة واحد للإصدارات/الأنواع. لا تحتاج إعادة كتابة — تحتاج Phase 1–4 بالأسفل.

---

## Performance Problems

قياسات فعلية: **0.08–0.11s/seed، RSS 4.0–4.9MB**، حتمية كاملة. الـ 15s timeout في الـ API متساهل جداً مقابل الواقع (جيد).

- **التعقيد**: viability checks ~3400/seed (مهيمن) + clustering O(n·(n+k²)) مع n≈350 وk≤~100 (~مليونات عمليات مسافة) + فرز O(k²) على ~90 candidate. مقبول تماماً حالياً.
- **`static Cluster candidates[4096]`** = ~67MB BSS نظرية (pages تُلمس فقط عند الاستخدام — القياس الفعلي 4.9MB) لكنه يمنع threading (H3) ونسخ 16KB لكل candidate.
- **السلّم (سؤال 9)**: بنموذج process-per-request: 10 مستخدمين متزامنين مريح؛ 100 متزامن ≈ 10 أنوية مشغولة (ممكن)؛ 1000 متزامن = ~100 عملية/ثانية × 5MB → **bottleneck = spawn عملية/طلب + عدم إمكانية التجميع داخل عملية واحدة بسبب static**. أولوية الحل: context struct (Phase 5) ثم pool من العمليات أو in-process خيوط + cache للـ seeds المكررة (فعّال جداً: نفس الـ seed يُعاد طلبه كثيراً في المنتجات الحقيقية).

---

## Accuracy Problems

مرتبة حسب الخطورة:
1. C1 (spawn ≠ origin) — منهجي لفئة كاملة.
2. H1 (~29% pyramid FPs) — يلوث counts/density لكل العناقيد.
3. C2 (حذف useful بالسقف) — false negative بنيوي.
4. مواقع chunk-origin (M6) — انحراف بصري داخل اللعبة حتى مئات البلوكات.
5. `density` upper-bound (M5).
6. **Explanations (سؤال 7)**: كل النصوص متسقة مع البوابات الحقيقية للفئات (تحققت من كل فرع بما فيه حافة `d_sw_ac=-1` — لا يمكن أن تتسرب) **عدا** نص Spawn Outpost (كاذب — C1)، وكلها تعرض ما هو في الحقيقة "توقع biome-based" بلغة "located" القطعية — overclaim بنيوي حتى مع C1 مصلحاً، لأن Cubiomes لا يثبت: توليد القرية كاملة (terrain/pieces)، الشعاب أو الـ shipwreck الفعلي، أو أن الـ Ancient City ولّد جيده (jigsaw بدون فحص terrain). الـ explanations تفتقر لمؤشر مستوى الثقة.
7. مصطلحات الواجهة: "دقة 100%"، "simulate terrain"، "World Spawn (0,0)" — غير صحيحة (C5).

---

## Security Problems

- **Command injection: نظيف.** الـ API يستخدم `spawn(binaryPath, args)` بلا shell (`route.ts`)، والأخطاء لا تسرّب مسارات. الـ CLI لا يستدعي shell.
- **Integer overflow: نظيف** في كود SeedLens (كل المسافات double، الحدود int صغيرة، الـ seed يمرر كـ bit-pattern صحيح لـ Java long). C3 هو خطأ parsing وليس overflow.
- **DoS**: زمن التحليل مستقل عن قيمة الـ seed (قياساً) → لا vector عبر الـ seed نفسه. الخطر: عدم وجود rate limit (M7) + غياب auth + timeout 15s و SIGKILL موجود ✓.
- **ذاكرة**: ASAN/UBSan نظيف على seeds عادية ومتطرفة (`INT64_MIN/MAX`)؛ نقطتا الفشل النظريتان في H4.
- **مسارات**: fallback مطلق `/home/pc/Desktop/SeedLens/seedlens` في `route.ts` — يعمل على هذا الجهاز فقط؛ لا خطر أمني بحد ذاته لكنه مضاد للـ portability.
- **خطر بسيط**: `spawn` يورّث environment ويشغّل binary من مسار قابل للكتابة في repo — داخل نموذج ثقة محلي مقبول.

---

## Testing Gaps

الموجود جيد كـ regression snapshots (5 حالات: baseline، C/D إيجابية، A/B سلبية) — لكن:
1. **يجمّد أخطاء**: `useful_clusters == 4` (انظر C2)؛ أي إصلاح للسقف سيكسر الاختبار ويجب تحديث القيم المقاسة (5 للـ baseline).
2. لا اختبارات سلبية للـ CLI: seed فارغ/غير رقمي/خارج النطاق (ERANGE)/`--version` بلا قيمة/args زائدة.
3. لا اختبارات لإصدار غير مدعوم عبر الـ API/CLI.
4. لا تحقق من schema الـ JSON كاملة (يقرأ حقولاً متفرقة فقط) ولا من invariants هندسية (كل structure داخل الحدود — فحصت يدوياً على 35 seed: نظيف، لكن يجب أن يكون اختباراً دائماً).
5. لا اختبارات للحتمية (run-to-run) ولا golden files للـ explanations.
6. لا unit tests للمحرك مباشرة (كل شيء عبر CLI) — `internal.h` يسمح بذلك بسهولة.
7. لا اختبار أداء (budget مثل <0.5s/seed).
8. لا تغطية لـ text seeds ولا لتحويل hashCode (طبقة الـ frontend بلا اختبارات أصلاً).
9. لا CI.

---

## Recommended Roadmap

### Phase 0 — هندسة أساس (قبل أي شيء)
- **الهدف**: قابلية التتبع والتكرار.
- **الملفات**: مستودع جديد `git init` + `.gitignore` (artifacts/build) + `Makefile` + (جديد) `third_party/` أو commit-pinned submodule لـ Cubiomes.
- **التغيير**: ربط Cubiomes إصداراً محدداً (`e61f905` حالياً)، بناءه ضمن `make`، والربط **static** بـ `libcubiomes_static.a` بدل `libcubiomes.so` من `~/Downloads`، وإزالة المسارات المطلقة من `Makefile` و`route.ts`.
- **السبب**: C4 — بلا هذا، كل إصلاح لاحق غير قابل للتحقق أو للتكرار على أي جهاز آخر.
- **المخاطر**: منخفضة؛ تغيير link قد يظهر warnings مخفية سابقاً.
- **الاختبار**: `make` نظيف على clone جديد + `ldd ./seedlens` لا يُظهر libcubiomes + تشغيل regression suite.

### Phase 1 — صدق المخرجات (Accuracy Integrity)
- **الهدف**: إزالة كل ادعاء غير مدعوم.
- **الملفات**: `engine/src/discovery.c`, `engine/src/analysis.c`, `engine/src/explanation.c`, `engine/include/seedlens.h`, `engine/src/json.c`, `frontend/src/lib/translations.ts`, `frontend/src/lib/minecraftTerrain.ts`, `frontend/src/components/*`, `docs/architecture.md`.
- **التغيير المقترح**: (أ) إما استدعاء `getSpawn` لكل seed وإصلاح `Spawn Outpost` فعلاً، أو إعادة تسمية كل شيء إلى `distance_to_origin` مع إفصاح — قرار منتج، ليس تقنياً؛ (ب) إضافة حقل `viability` لكل structure: `biome_viable` / `terrain_checked` بعد تفعيل `isViableStructureTerrain` للأهرامات/المعابد، و`biome_only` للبقية؛ (ج) تعديل نصوص الـ explanations والواجهة ("estimated/biome-validated" بدل "located"، إزالة "100%")، وإما إخفاء الخريطة الوهمية أو وسمها بوضوح كـ "illustrative only".
- **السبب**: C1 + C5 + H1 — ثقة المستخدم والمقارنة باللعبة الحقيقية.
- **المخاطر**: تغيير الـ JSON قد يكسر مستهلكين حاليين → أضف حقولاً جديدة دون حذف القديمة، مع `schema_version`.
- **الاختبار**: وحدة تختبر أن كل `Spawn Outpost` الفئة تتحقق بمعيارها المعلن؛ عينة seeds تُقارن مع generate داخلي فعلي إن توفر؛ اختبار إنكار خريطة وهمية (سمة visible في الـ UI).

### Phase 2 — اكتمال النتائج (سقف/ترتيب)
- **الهدف**: لا حذف صامت.
- **الملفات**: `engine/src/cluster.c`, `engine/src/engine.c`, `engine/include/seedlens.h`, `tests/test_regression.py`, `docs/architecture.md`.
- **التغيير المقترح**: تقييم usefulness **قبل** الاقتطاع (الفلترة ثم ترتيب النتائج useful ثم سقف عرض configurable)؛ إضافة إلى الـ JSON: `candidate_clusters`, `clusters_truncated: bool`, `total_useful_found`. توثيق السلوك.
- **السبب**: C2 (مقاساً: 18/20 seeds تخسر نتائج).
- **المخاطر**: تغيّر أرقام الـ regression المعروفة (متوقع ومطلوب)؛ زيادة حجم المخرجات (متوسط 8.6 useful — لا مشكلة).
- **الاختبار**: baseline seed يجب أن يُخرج 5 useful (المقاسة)؛ invariant: `total_useful_found` لا يتأثر بالسقف؛ اختبار seed بكثافة عالية يثبت عدم الحذف.

### Phase 3 — صرامة الإدخال والـ JSON
- **الهدف**: نفس الإدخال = نفس العالم في كل الطبقات.
- **الملفات**: `cli/main.c`, `engine/src/json.c`, `frontend/src/app/api/analyze/route.ts`, `tests/`.
- **التغيير**: رفض ERANGE صراحة (exit code مخصص)؛ توحيد سياسة text seeds (إما دعم hashCode في الـ CLI بخيار، أو رفضها في الـ frontend أيضاً — بقرار واحد موثق)؛ إخراج `seed` كـ **string** في الـ JSON مع الحفاظ على حقل رقمي legacy؛ exit codes: 0 نجاح، 1 خطأ إدخال، 2 إصدار، 3 خطأ داخلي.
- **السبب**: C3 + H2 + H5.
- **المخاطر**: كسر مستهلكي الـ JSON الحاليين → إبقاء `seed` الرقمي + إضافة `seed_exact`.
- **الاختبار**: 20-nines → خطأ لا سقاط؛ INT64_MIN/MAX → نجاح بقيمة صحيحة؛ text seed → سلوك موحد في CLI وAPI؛ seed > 2⁵³ يصل الـ UI سليماً.

### Phase 4 — معمار الإصدارات
- **الهدف**: إضافة إصدارات دون لمس منطق التجميع/التقييم.
- **الملفات**: (جديد) `engine/src/version.c` + `version.h`، تعديل `discovery.c`, `engine.c`, `cli/main.c`, `frontend/src/lib/validation.ts`, `tests/`.
- **التغيير**: جدول `VersionProfile { mc_enum, targets[], notes }` يُشتق منه كل شيء (قائمة واحدة للحقيقة)؛ إزالة `target_meta` المكرر؛ التحقق من `getStructureConfig(type, mc)` لكل target لكل إصدار.
- **السبب**: سؤال 12 — الحالي يتطلب تعديلات متفرقة في 4 طبقات.
- **المخاطر**: configs الإصدارات الأقدم (pre-1.18 villages إلخ) تحتاج تحققاً مقابل vanilla — **Needs empirical validation** لكل إصدار قبل الاعتماد.
- **الاختبار**: لكل إصدار: golden seeds بنتائج معروفة (من العالم الحقيقي أو cubiomes tests) + فشل صريح للإصدارات غير المدعومة.

### Phase 5 — الجاهزية للخدمة
- **الهدف**: concurrency آمن + cache.
- **الملفات**: `engine/src/engine.c`, `cluster.c`, `discovery.c`, (جديد) `server/` أو تعديل `route.ts`.
- **التغيير**: تحويل المخازن static إلى context struct (توقيع API يصبح `seedlens_analyze_ctx(ctx, …)`)؛ LRU cache (seed,version)→JSON في الـ API؛ pool من العمليات كبديل سريع.
- **السبب**: H3 + متطلبات 100+ req/s المستقبلية.
- **المخاطر**: متوسطة (تغيير توقيع API داخلي)؛ ثنائية pool/context.
- **الاختبار**: stress test متوازٍ (TSAN + مقارنة نتائج متزامنة مع متسلسلة).

### Phase 6 — ميزات مستقبلية (سؤال 13)
- **P0**: إكمال Phase 1–3؛ cache للـ seeds.
- **P1**: أنواع إضافية (Outpost, Monument, Mansion, Trail Ruins؛ Trial Chambers مع 1.21)؛ تحليل biomes حول العنقود (من Cubiomes مباشرة)؛ rate limiting؛ schema versioning.
- **P2**: terrain-aware scoring كامل؛ تحليل Spawn biome؛ Nether/End (Fortress/Bastion/End City)؛ تقارير "rare combinations"؛ batch API موازٍ.
- **P3**: موارد قريبة (خشب/غذاء حول العنقود)؛ إمكانيات مهن القرويين؛ **خريطة حقيقية** بتوليد tiles من Cubiomes (تستبدل الـ noise الوهمي)؛ SDK/TypeScript client.

---

## ملاحظات ختامية

- **ما هو جيد فعلاً**: نظافة الكود، الحتمية المقاسة، السرعة، سلامة الذاكرة تحت ASAN، فصل الوحدات، استقرار المخرجات، واختبارات regression موجودة أصلاً — قاعدة أفضل بكثير من المتوسط لمشروع بهذا الحجم.
- **لا أوصي بأي تغيير في thresholds الآن**: القواعد الخمس مطبقة كما هي موثقة بالضبط في `docs/architecture.md`، ولم أجد دليلاً قياسياً على أن عتبة معينة خاطئة — المشكلات المكتشفة أعلاه (C1/C2/H1) تلوث القياسات التي ستُبنى عليها أي معايرة. أولاً يُصلح القياس، ثم تُعاير العتبات على dataset مصنفة (مثلاً 50–100 seed بتصنيف بشري/من اللعبة) — قبل ذلك أي تعديل عتبة سيكون تخميناً: **Needs empirical validation**.
- ملفات التحقق المستخدمة (للمقارنة/إعادة التشغيل): `/tmp/audit_check.c`, `/tmp/audit_engine/` (cap=500), `/tmp/audit_engine20/` (cap=20), `/tmp/seedlens_asan`.
