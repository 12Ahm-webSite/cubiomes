# SeedLens — تقرير Audit مستقل (بدون تعديل أي ملف)

> نطاق الفحص: `engine/` + `cli/main.c` + `tests/` + `docs/architecture.md` + `README.md` + `Makefile` + `frontend/src/{app/api,lib,types,components}` مقابل Cubiomes في `/home/pc/Downloads/cubiomes` (commit `e61f905`).
> المنهج: قراءة source كامل + تشغيل read-only للـ binary (`seedlens`) للتحقق من السلوك. لم يُعدَّل أي ملف.

---

## Executive Summary

**ما حالة المحرك؟**

SeedLens محرك صغير ونظيف (~1200 سطر C) فوق Cubiomes، وخط الأنابيب مفهوم: `discovery → cluster → analysis → explanation → JSON`. جودة التنفيذ الأساسية جيدة: فصل الوحدات مقبول، لا يوجد `command injection` في الـ API (يستخدم `spawn` بدون shell)، الـ stdout نقي JSON والـ stderr للأخطاء، والأداء الحالي ~0.11–0.13 ثانية/seed (قياس فعلي على 5 seeds) وهو كافٍ للاستخدام المحلي.

لكن **المحرك ليس جاهزاً لـ production كمنتج يدّعي الدقة**، لسبب مركزي واحد يتفرع عنه معظم المشاكل:

> **المشروع لا يفرّق بين `candidate` و `viable` و `confirmed`، ويقدّم `nearest_spawn` وهو في الحقيقة المسافة إلى `(0,0)` وليس إلى spawn الحقيقي، ولا يستدعي `isViableStructureTerrain()` إطلاقاً.**

إلى جانب ذلك توجد: ثغرة تحقق دخل (overflow clamp صامت)، وسقف `MAX_CLUSTERS=20` يُسقط نتائج بصمت، وbuffers ثابتة تمنع الـ concurrency داخل العملية، وthresholds من نوع magic numbers بلا اشتقاق موثق، وتضارب CLI/Frontend في معالجة الـ seed، وخريطة frontend تعرض terrain اصطناعياً وليس حقيقياً.

**الخلاصة:** بنية صالحة كـ prototype قوي، وغير صالحة كمنتج دقيق قبل Phase 1 أدناه.

---

## Critical Problems (يجب إصلاحها قبل production)

### C1. `nearest_spawn` ليس spawn — بل المسافة إلى الأصل `(0,0)`

- `engine/src/discovery.c:72`: `distance = sqrt(x²+z²)` — أي المسافة إلى `(0,0)`.
- `engine/src/cluster.c:47-50`: `nearest_spawn = min(records[i].distance)` — نفس الشيء.
- Cubiomes يملك `estimateSpawn()` / `getSpawn()` (`finders.h:307-316`) والمشروع لا يستدعي أياً منهما.
- فئة `Spawn Outpost` (`analysis.c:59-88`, شرط `nearest_spawn <= 1000`) هي إذن فئة **"Origin Outpost"** وليست قرب spawn الحقيقي. الـ explanation في `explanation.c:79-83` يقول "Located X blocks from world spawn" — **ادعاء غير صحيح**.
- التأثير: false positives/negatives منهجي لفئة كاملة. أي seed يكون spawn الحقيقي فيه بعيداً عن `(0,0)` بمئات البلوكات سيُصنّف خطأً.
- الإصلاح يتطلب استدعاء spawn الحقيقي لكل seed (مكلف: `getSpawn` بطيء حسب توثيق Cubiomes) أو إعادة تسمية الحقل/الفئة إلى `distance_to_origin` مع إفصاح صريح. **Needs empirical validation** لمقدار انحراف spawn عن الأصل في 1.20.6 عبر عينة seeds.

### C2. لا يوجد تمييز candidate / viable / confirmed — والواجهة تدّعي تأكيداً ضمنياً

- `discovery.c:58-61` يستدعي `isViableStructurePos(..., 0)` فقط. هذا في Cubiomes **فحص biome فقط** (انظر `finders.c:1434-1810`)، وليس توليد قطع (pieces) ولا loot ولا terrain كامل.
- المشروع لا يستدعي `isViableStructureTerrain()` إطلاقاً (grep على المشروع: صفر نتائج) رغم أن توثيق Cubiomes (`finders.h:363-371`) ينص صراحة أن `Desert_Pyramid` و `Jungle_Pyramid` (و `Mansion`) في 1.18+ تتطلب ارتفاع سطح كافٍ عند الزوايا الأربع، وأن Cubiomes لا يوفر الارتفاع لكن يوفر تقريباً عبر هذه الدالة. النتيجة: **false positives منهجية للأهرامات/المعابد** التي تفشل شرط الارتفاع.
- `Ancient_City` (`finders.c:1770-1787`): فحص jigsaw biome فقط. لا تحقق من erosion/deep-dark الفعلي الكامل ولا من توليد القطع.
- `Village` في 1.18+ (`finders.c:1613-1632`): حلقة variants صحيحة، لكنها عينة biome مركزية — لا تضمن قرية مكتملة (أسرّة/مهن/terrain صالح لكل القطع).
- الـ JSON يعرض `structures: [{type,x,z}]` بدون أي حقل `viability_level` أو `confidence` أو disclaimer. المستخدم سيفسرها كمواقع مؤكدة.
- المطلوب: حقل صريح (مثلاً `viability: "biome-check-only"`) + صياغة explanation تتضمن عدم اليقين + استدعاء `isViableStructureTerrain` على الأقل للأنواع المتأثرة.

### C3. تجاوز نطاق int64 يُلتقط بصمت (silent clamp) — ثغرة تحقق دخل

- `cli/main.c:31` يستخدم `strtoll` بدون فحص `errno`/`ERANGE`.
- مُثبت فعلياً: `./seedlens 99999999999999999999999` نجح (`rc=0`) وأخرج `"seed": 9223372036854775807` — أي أن دخلاً خارج النطاق حُسب على أنه `LLONG_MAX` بصمت. المستخدم يرى نتيجة لـ seed مختلف تماماً عن ما أدخل دون أي تحذير.
- الإصلاح تافه (فحص `errno == ERANGE`) لكن خطورته عالية لأنه **فساد بيانات صامت**.

### C4. سقف `SEEDLENS_MAX_CLUSTERS=20` يُسقط عناقيد بصمت ويُسمّي الناتج "total"

- `engine/include/seedlens.h:13`, `engine.c:53`: `total_clusters = cluster_count` حيث `cluster_count` هو العدد **بعد** إزالة التكرار **وبعد** السقف 20.
- مُثبت: seed `123456789` يُرجع `total_clusters=20` بالضبط (قيمة السقف)، وseed `+123` يُرجع `total_clusters=20` أيضاً. أي أن العدد الحقيقي للمرشحين غير معروف — قد يكون 25 أو 60.
- الأسوأ: الترتيب قبل السقف يتم بمعيار `cluster_better` (type_count ثم count ثم nearest_spawn الأصغر) — وهو ليس نفس معايير "usefulness". عنقود useful حقيقي ترتيبه 21+ سيُحذف قبل التقييم. وهذا **false negative بالتصميم** وغير موثق في `docs/architecture.md`.
- الـ IDs غير متسلسلة (`[4,10,11,19]` لـ seed 123456789) مما يؤكد أن `id = i+1` هو فهرس المرشح الأصلي لا ترتيب Useful — مربك للـ frontend إذا افترض تسلسلاً.

### C5. buffers ثابتة (`static`) تمنع الـ concurrency داخل العملية

- `engine.c:45,49`: `static StructureRecord records[4096]` و `static Cluster clusters[20]`.
- أي استدعاءين متزامنين لـ `seedlens_analyze` في نفس العملية (مستقبل API multithreaded، أو اختبارات متوازية) سيُفسد أحدهما الآخر (data race). التصميم الحالي آمن فقط كنموذج process-per-request (وهو ما يفعله frontend عبر `spawn`)، لكنه **يمنع أي تطور نحو in-process server** الذي ستحتاجه لـ 100+ req/sec.
- الإصلاح: تخصيص لكل استدعاء (caller-provided buffers أو malloc) — تغيير API صغير لكنه كاسر.

---

## High Priority

### H1. تضارب CLI ↔ Frontend في معالجة الـ seed

- Frontend `validation.ts:47` يقبل فقط `^-?\d+$` (يرفض `+123` والمسافات، ويحوّل أي نص عبر `javaStringHashCode`)، بينما CLI `main.c:31` يقبل `+123` عبر `strtoll` (مُثبت: `./seedlens +123` نجح وأعطى seed 123) ويرفض `"  123  "` (مُثبت `rc=1`) بينما Frontend يعمل `trim()`.
- النتيجة: نفس الدخل المنطقي (`+123`) يُفسَّر رقمياً في CLI وكنص مُجزّأ (hash) في الـ UI — سلوكان مختلفان لنفس الزر. وCLI لا يقبل seeds نصية إطلاقاً (يرفضها كـ invalid) بينما الـ UI يحوّلها — أي أن طبقة التحويل موجودة في المكان الخطأ (UI بدل المحرك).
- التوصية: توحيد القاعدة في مكان واحد (الأفضل: المحرك يقبل canonical int64 فقط، والتحويل النصي موثق ومُختبر في طبقة واحدة).

### H2. تحيز الحافة (boundary bias) غير معلَن

- الاكتشاف يقصّ عند `±5000` (`discovery.c:52-56`)، لكن التجميع (`cluster.c`) يبني عناقيد فقط من السجلات **داخل** المربع. عنقود حقيقي يعبر الحد (مثلاً مركزه عند 4900 ويمتد إلى 5400) سيظهر ناقص العدد/القطر/الكثافة، وقد يفشل thresholds ظلماً. لا يوجد أي تحذير `truncated_at_edge` في JSON. هذه false negatives منهجية قرب الحواف الأربع.

### H3. إزالة التكرار تدمج عناقيد متميزة وتُبقي مكررات

- `cluster.c:127-164`: يُعتبر عنقودان مكررين إذا `center_distance < 250` **أو** overlap ≥ 60% من الأصغر.
- مشكلتان متعاكستان:
  - عنقودان حقيقيان متميزان على بعد 200م بأعضاء مختلفة كلياً سيُدمجان خطأً (الشرط الأول وحده يكفي).
  - عنقودان يتشاركان أعضاء (مُثبت فعلياً: clusters 5 و6 لـ seed `123` يتشاركان Village عند `(4368,3632)` و Ancient City عند `(4432,3600)` — overlap 40% ومسافة مركزين ~481م فبقيا منفصلين) يظهران للمستخدم كعنقودين "مختلفين" بنفس الهياكل — مربك.
- العتبتان (250م، 60%) magic numbers بلا اشتقاق. **Needs empirical validation**.

### H4. المركز = متوسط حسابي — حساس للقيم الشاذة

- `cluster.c:54-66`: `center = mean(x), mean(z)`. نقطة واحدة بعيدة تسحب المركز خارج الكتلة الفعلية، فيُعرض للمستخدم مركز في منطقة فارغة. البديل (medoid أو geometric median) أكثر تمثيلاً لخرائط الاستكشاف. تأثيره متوسط لكنه يمس كل عنقود.

### H5. الكثافة (`density`) معرفة بدائرة مثالية من القطر — ومعروضة بدقة زائفة

- `cluster.c:99-105`: `density = count / (π·(diameter/2)²)`. هذا يفترض أن العنقود دائرة قطرها max-pairwise — للعناقيد الممتدة (elongated) المساحة الحقيقية (convex hull) أصغر بكثير، فالكثافة **مُقللة منهجياً** للأشكال الخطية.
- الطباعة `%.8f` (`json.c:155`) تعطي 8 منازل عشرية لقيم بمقياس `1e-5` — أي ~3 أرقام معنوية فقط، والبتّ عند العتبات (`0.00001000` إلخ) يتأثر بالتقريب العشري للمسافات (`%.1f`). العتبات تبدو "مُعايرة بدقة 8 منازل" وهي في الحقيقة ضجيج قياس. (ملاحظة تصحيحية: لا يوجد تباين binary/source — التحققت بإعادة البناء إلى `/tmp` وكانت المخرجات `IDENTICAL`؛ ما ظهر علمياً في طرفية سابقة كان `repr` بايثون فقط.)

### H6. خريطة الـ FrontendTerrain وهمية — ليست بيانات Minecraft الحقيقية

- `frontend/src/lib/minecraftTerrain.ts`: مولّد Simplex Noise خاص (LCG + Fisher-Yates) بمعاملات اعتباطية (`C/1800, E/950, ...`) وتصنيف biomes بقواعد يدوية. لا علاقة له بضجيج 1.20 الحقيقي ولا بـ seed الفعلي إلا كـ hash عرضي (`hashSeedToUint32` — وليس حتى `javaStringHashCode` المستخدم في `validation.ts`!).
- إذا عُرضت هذه الخريطة للمستخدم كخلفية لمواقع الهياكل، فهي **مضللة بصرياً**: قارئ سيربط بصرياً بين الهيكل والـ biome المعروض وهما غير مرتبطين. يجب وسمها "stylized preview — not actual terrain" أو إزالتها أو استبدالها ببلاطات حقيقية من المحرك. راجع `SeedMapInner.tsx` للتأكد من عدم وجود ادعاء "real biome".

---

## Medium Priority

### M1. التصنيفات (v3.1) binary بدون score — والترتيب لا يعكس الفائدة

- لا يوجد numeric score؛ العنقود إما useful أو لا. داخل Useful لا يوجد ترتيب (يُحفَظ ترتيب المرشحين الأصلي). مستخدِم يرى 7 عناقيد (seed `+123`) لا يعرف أيها "أفضل".
- أولوية `primary_category` (`explanation.c:3-26`): Mega > Trading > Abyssal > Spawn > Dense. غير مبررة: هل عنقود Mega-Hub من 5 حطام سفن فعلاً أهم من Metropolis بقريتين؟ **Needs empirical validation** (تفضيلات لاعبين حقيقية).
- استثناء v3.1 (`analysis.c:110-113`: قطر حتى 800 إذا `closest<=150`) يخص Mega-Hub فقط — لماذا لا تُطبق فلسفة "compact exception" على الفئات الأخرى؟ غير متسق.

### M2. تداخل الفئات (overlap) يضخّم الظاهرة

- نفس العنقود يحمل عدة فئات routinely (مُثبت: `+123` cluster 5 يحمل 3 فئات). لا بأس بذاته، لكن مع `useful_clusters` الذي يعدّ العناقيد لا الفئات، قد يُساء فهم "7 useful" على أنها 7 فرص متميزة بينما 5 و6 متداخلان جغرافياً. يُنصح بمقياس تنوع/تفرد.

### M3. مقاييس المسافة كلها XZ فقط — تتجاهل Y والعبور الفعلي

- `distance_xy` ثنائية الأبعاد. `Abyssal Hub` يدّعي "surface-to-underground opportunity" (`explanation.c:62-67`) بمسافة XZ ≤ 450م، بينما الـ Ancient City على `y≈-42` والسطح على `y≈70` — أي 110 بلوك عمودياً + كهوف/تضاريس غير مضمونة. المسافة الأفقية لا ترتبط خطياً بزمن الوصول. على الأقل يجب ذكر "horizontal distance only".

### M4. تغطية الهياكل ضيقة (5 أنواع فقط) وتحيّز بحري

- `discovery.c:3-9`: Village, Desert Pyramid, Jungle Temple, Shipwreck, Ancient City. الغائب: Outpost, Monument, Mansion, Ruined Portal, Trail Ruins, Trial Chambers (الأخير 1.21+ لكن البنية يجب أن تتسع له)، Stronghold, Mineshaft.
- عملياً: Shipwreck شائع بحرياً فيهيمن على العناقيد (مُثبت: كل عناقيد seed 123456789 تحتوي Shipwreck). الفئات "Trading/Abyssal" منحازة بحرياً بالتبعية. هذا خيار scope مشروع، لكن يجب توثيقه كـ limitation لا كـ "analysis engine" عام.

### M5. لا يوجد `--help`، وترتيب الوسائط صارم، ورسائل الخطأ إنكليزية فقط

- `./seedlens --help` → `invalid seed '--help'` (`rc=1`). لا يوجد usage مفصّل ولا `--version` بدون قيمة تُكتشف بشكل نظيف. الـ frontend عربي/إنكليزي لكن CLI إنكليزي فقط — مقبول للـ backend لكن يجب توثيقه.

### M6. البناء غير محمول (non-portable build)

- `Makefile:3-4`: `CUBIOMES_DIR` مطلق (`/home/pc/Downloads/cubiomes`) + `rpath` مطلق. أي نشر خارج هذه الآلة ينكسر (وهو ما يفسر fallback المسار المطلق في `route.ts:16`). لا يوجد تثبيت لـ Cubiomes version pin (يُبنى ضد `build/` الحالي). يُنصح بـ submodule أو vendored hash + `CUBIOMES_DIR` نسبي.

---

## Low Priority

- `L1`. `type_pair_distance` تُرجع `-1.0` عند عدم الوجود (`analysis.c:56`) — آمن حالياً لأن كل استدعاء يفحص `> 0.0`، لكن العقدة هشة (قيمة -1 كـ sentinel). الأفضل `bool` + out-param.
- `L2`. `jbuf_printf` (`json.c:50-65`) يستخدم `tmp[1024]` ثم `memcpy(written)` — إذا تجاوز `vsnprintf` الـ 1024 يُقتطع بصمت (truncation) دون كشف. عملياً الرسائل قصيرة، لكنها قنبلة مستقبلية للـ explanations الطويلة.
- `L3`. `cli/main.c:32` يفحص `*endptr != '\0'` لكن بعد `strtoll` الذي يتخطى المسافات البادئة — سلوك غير متسق مع الرفض الصارم للمسافات اللاحقة. توحيد مطلوب (H1 يغطيه).
- `L4`. `Makefile: LDFLAGS = -Lengine -lseedlens` يعتمد على تطابق الاسم مع `libseedlens.a` — يعمل لكنه هش؛ الأوضح ربط المسار مباشرة.
- `L5`. لا يوجد إصدار للـ JSON schema (`"schema_version"` غائبة) — أي تغيير مستقبلي سيكسر الـ frontend بصمت.
- `L6`. الـ `frontend` يحمل `node_modules` و `.next` داخل الشجرة — يضخّم أي فحص/نسخ. يجب إخراجها من الـ audit scope مستقبلاً عبر `.gitignore` + عدم الشحن.

---

## Algorithm Problems (تفصيل)

| # | الخوارزمية | الموقع | المشكلة | التأثير |
|---|-----------|--------|---------|---------|
| A1 | Discovery region iteration | `discovery.c:40-75` | `region_range = R/(regionSize*16)+2` آمن (لا miss) لكنه يولّد ~2500 استدعاء viability لكل seed — مقبول. المشكلة ليست هنا بل في **السقف الصامت** `MAX_STRUCTURES=4096` (`discovery.c:63-66`): `return` بدون خطأ. حالياً N~مئات فلا يُضرب، لكن أي توسيع لل bounds سيُسقط بيانات بصمت | مستقبلي |
| A2 | Clustering | `cluster.c:3-112` | Single-pass radius (كل سجل مركز، نصف قطر 500) + رفض قطر>800. هذا **ليس DBSCAN ولا k-means** بل "ego-networks" — يُنتج O(N) مرشحاً شديد التداخل ثم يعتمد على dedup هش. السلاسل (chain: A-B-C حيث AB<500 وBC<500 لكن AC>800) تُرفض أو تُشطر اعتباطياً حسب المركز المختار | false +/- على السلاسل |
| A3 | Ranking | `cluster.c:114-125` | `type_count > count > nearest_spawn(الأصغر)` — لا علاقة له بالفائدة الفعلية. عنقود useful كبير قد يتأخر خلف عنقود غير-useful متنوع ثم يُقطع بالسقف 20 | C4 |
| A4 | Dedup | `cluster.c:127-164` | عتبتان اعتباطيتان (H3) | دمج/تكرار |
| A5 | Density | `cluster.c:99-105` | دائرة مثالية (H5) | تحيّز ضد الممتد |
| A6 | Center | `cluster.c:54-66` | متوسط (H4) | مركز مضلل |
| A7 | Category thresholds | `analysis.c:59-195` | 12+ عتبة رقمية (1000/720/780/800/600/450/250/180/150/100 + 3 كثافات) بلا اشتقاق/CI/ablation في `docs/`. بعضها زائد عن الحاجة (الكثافة دالة في القطر والعدد أصلاً — تحقق من الترابط قبل الاحتفاظ بها) | overfit على regression الحالية |
| A8 | Complexity | — | Discovery: O(R²) ثابت (~2500 viability، كل واحدة biome sampling مكلف — هذا الـ bottleneck). Clustering: O(N²) مسافات + O(K²) pairwise حيث K حجم العنقود. JSON: O(useful·K). الإجمالي الحالي ~0.1s — جيد. لكن **لا يوجد memoization** للـ Generator بين الأنواع (يُعاد `applySeed` ضمنياً؟ لا — مولد واحد يُعاد استخدامه، جيد) ولا بين الطلبات (يُعاد `setupGenerator` كل مرة) | انظر Performance |

**ملاحظة عن thresholds:** لا أوصي بتغيير أي عتبة الآن. أي تعديل يتطلب: (1) المشكلة بمثال seed حقيقي، (2) الدليل (مقارنة vanilla/client)، (3) التأثير المتوقع على الحالات الأربع المحمية + عينة أوسع، (4) test cases جديدة (موجبة/سالبة/حدّية). الوضع الحالي overfit على 4 regression cases — تغيير عشوائي سيكسرها دون أن يعني تحسناً.

---

## Architecture Problems

1. **المعمارية مناسبة كـ prototype، ناقصة كمنتج:** الحدود (`discovery/cluster/analysis/explanation/json/engine`) نظيفة والاعتماد أحادي الاتجاه — **جيد**. لكن: لا طبقة `version` مجردة، لا طبقة `viability` تُرجع مستويات ثقة، لا `scoring` رقمي، لا `cache`، لا `concurrency` آمنة. الإضافة المستقبلية (1.21.x، Nether، End) ستلمس `engine.c` + `internal.h` + thresholds معاً — أي coupling عملي رغم الفصل الشكلي.
2. **أفضل abstraction للإصدارات:** `VersionDescriptor { version_string, mc_enum, search_defaults, structure_table* (5+ أنواع مع region/salt/viability_fn), thresholds* per-version, spawn_strategy }` + جدول `SUPPORTED_VERSIONS[]` بدل `strcmp(ver,"1.20.6")` في `engine.c:30`. الـ discovery عام أصلاً عبر `getStructureConfig` (**جيد** — لن يحتاج إعادة كتابة لذلك الجزء)، لكن thresholds والـ spawn يجب أن يأتيا من الجدول.
3. **الـ frontend يتجاوز المحرك:** تحويل النص→seed في `validation.ts` (مع `javaStringHashCode` الخاص به) + terrain وهمي في `minecraftTerrain.ts` + مسار binary مطلق في `route.ts` — ثلاث مسؤوليات يجب أن تكون في المحرك/التوثيق لا في الـ UI.
4. **لا عقدة schema:** لا `schema_version` في JSON، والـ TS types (`seedlens.ts`) يدوية — أي انحراف مستقبلي (إضافة حقل `viability`) سيُكتشف في runtime فقط.

**هل المعمارية الحالية مناسبة لبناء منتج حقيقي؟** نعم كأساس، بشرط Phase 1–2 أدناه. نقاط الضعف المركزية: غياب طبقة الثقة (C2)، غياب طبقة الإصدارات، والـ static state (C5).

---

## Performance Problems

- **القياس الفعلي:** ~0.11–0.13s/seed أحادي النواة (5 seeds متنوعة بما فيها `INT64_MIN/MAX`). أي ~8 req/sec لكل core كحد أقصى نظري.
- **أين الـ bottleneck:** `isViableStructurePos` (biome sampling متعدد الطبقات لكل مرشح، ~2500×seed). الـ clustering تافه نسبياً (N صغير). `setupGenerator` لكل طلب مكلف وغير قابل لإعادة الاستخدام عبر الطلبات (single-threaded Cubiomes generator غير آمن للمشاركة بدون نسخ).
- **التوسع المستقبلي:**
  - 10 مستخدمين متزامنين: process pool من ~4–8 workers يكفي (latency ~0.5s p95) — **ممكن اليوم** عبر الـ API الحالي (spawn per request + timeout 15s الموجود في `route.ts:102-105` — جيد).
  - 100 متزامن: تحتاج pool + queue + rate limit + cache (نفس الـ seed يُعاد حسابه كل مرة حالياً — لا cache إطلاقاً). الـ spawn لكل طلب (~ms) + ~0.1s CPU = ~10 cores مشبعة.
  - 1000 متزامن: مستحيل بهذه البنية (CPU-bound + fork storm). تحتاج: precompute/caching دائم، تقليص bounds افتراضياً، sharding، وربما خدمة C persistent بدل fork-per-request.
- **إهدار مثبت:** لا cache، لا reuse للـ generator، `type_pair_distance` تُعاد لكل فئة (O(K²) مكررة 5×)، والـ frontend يُعيد `sampleTerrain` بكثافة للخريطة الوهمية (CPU في المتصفح بلا قيمة حقيقية).

---

## Accuracy Problems (ملخص مركز)

1. **Spawn ≠ spawn** (C1) — خطأ تسمية/قياس.
2. **Viability = biome-check فقط** (C2) — لا terrain، لا pieces، لا ضمان.
3. **XZ فقط** (M3) — بخاصة Abyssal Hub.
4. **Explanations تبالغ:** "makes it useful for exploration and progression" و "can support trading..." و "creating a compact surface-to-underground opportunity" — كلها تستنتج فائدة لعب فعلية من قرب هندسي خام دون (loot/مهن/تضاريس/مسار). يجب تخفيف الصياغة إلى "structures are located within X blocks of each other" + إخلاء مسؤولية.
5. **التحيز البحري** (M4) — Shipwreck في كل عنقود تقريباً؛ الفئات تكافئ الكثرة لا الجودة.
6. **Dense Multi يعرض `density` برقم خام** (`explanation.c:87-91`) — غير مفهوم للاعب ولا يقارَن بشيء.
7. **"Rigorously calibrated" في README** — غير مدعوم بأي منهجية منشورة في `docs/`. صياغة تسويقية في تقرير تقني.

أي شيء لم أتحقق منه vanilla (مواقع فعلية داخل اللعبة) وسمته أدناه — **Needs empirical validation**: مطابقة إحداثيات Cubiomes للعبة 1.20.6 لكل نوع من الخمسة، وانحراف spawn الحقيقي عن `(0,0)`، ومعدل false-positive للأهرامات بدون `isViableStructureTerrain`.

---

## Security Problems

- **لا command injection** في المسار الرئيسي — **جيد**: `route.ts:96` يستخدم `spawn(binaryPath, args[])` بدون shell، والـ seed رقمي والـ version من قائمة بيضاء.
- لكن:
  1. **DoS حسابي:** لا rate limit ولا concurrency cap ولا cache في `route.ts`. مهاجم يرسل N طلبات متوازية يشعل N عملية `seedlens` (كل واحدة ~0.1s CPU + biome gen). الـ timeout 15s يحدّ من التعلق لا من الإغراق. مطلوب: p-limit للتوازي + LRU cache + body size limit (موجود ضمنياً عبر Next).
  2. **C3 overflow الصامت** (فساد بيانات، ليس RCE).
  3. **`jbuf` بلا فحص realloc فاشل** (`json.c:31-37,50-65`): عند OOM يُكمل الكتابة في `b->data` القديم — نظرياً heap corruption / truncated JSON. عملياً المخرجات صغيرة (<100KB)، لكن يجب فحص NULL.
  4. **مسارات مطلقة + `rpath` مطلق** — خطر تحميل `libcubiomes.so` خاطئة إذا زُرعت في `CUBIOMES_BUILD`. التثبيت يجب أن يستخدم مسارات نسبية/موقعة.
  5. **Seedlens CLI بلا حدود موارد** (وقت/ذاكرة) عند التشغيل اليدوي — مقبول محلياً، لكن كخدمة يجب تغليفه (cgroups/timeout).
- لا مشاكل stack overflow واضحة (المصفوفات الكبيرة `static` لا على الـ stack — **جيد** من هذه الناحية)، ولا `strcpy`/`sprintf` غير آمن (يُستخدم `snprintf` — **جيد**).

---

## Testing Gaps

**الموجود (5 assertions فعلية):** baseline واحد (`123456789` → 20/4) + 4 regression (C, D موجبة؛ A, B سالبة). البناء يُعاد؟ لا — `test_cli.sh` يبني فقط إذا غاب الـ binary، فيختبر binary قديماً محتملاً.

**المفقود:**

1. ❌ اختبارات دخل فاسد CLI: overflow (`999...`)، `+123` مقابل `-0`، مسافات، `0x10`، فارغ، `--help`، `--version` مفقودة القيمة، ترتيب وسائط معكوس.
2. ❌ حدود int64: `INT64_MAX/MIN`، `0`، `±1` — تُنفذ لكن بلا assertions.
3. ❌ إصدارات: مرفوضة (`1.19`) بلا اختبار؛ ولا اختبار للقيمة الافتراضية عند غياب `--version`.
4. ❌ حتمية: نفس الـ seed مرتين → `diff` يجب أن يكون فارغاً.
5. ❌ فصل stdout/stderr + exit codes لكل حالة خطأ.
6. ❌ صلاحية JSON schema (json.load + فحص الحقول لكل عنقود — موجود جزئياً فقط).
7. ❌ وحدات: لا اختبار لـ `build_cluster` (سلسلة/حدود/نوع واحد)، ولا لـ `same_cluster`، ولا لكل فئة عند العتبة تماماً (±1 بلوك).
8. ❌ Frontend: `javaStringHashCode` مقابل Java الحقيقي (قيم مرجعية معروفة)، و`validateAndResolveSeed` (`+123`، مسافات، خارج النطاق)، و`route.ts` (mock للـ binary).
9. ❌ تحقق vanilla: صفر مقارنة مع اللعبة/Chunkbase — كل "accuracy" حالياً self-referential.
10. ❌ أداء: لا اختبار زمني (regression على >1s) ولا اختبار توازٍ.

---

## Recommended Roadmap

### Phase 1 — صدق البيانات (P0 — قبل أي production)

- **الهدف:** إيقاف الادعاءات الكاذبة وفساد الدخل الصامت.
- **الملفات:** `cli/main.c`، `engine/src/{discovery,cluster,engine,explanation,json}.c`، `engine/include/seedlens.h`، `docs/architecture.md`، `frontend/src/lib/validation.ts`.
- **التغيير:**
  1. إصلاح `strtoll` (فحص `errno==ERANGE` + رفض صريح، `C3`).
  2. إعادة تسمية `distance_to_spawn`/`nearest_spawn` إلى `distance_to_origin` **أو** دمج spawn الحقيقي (قرار تصميمي موثق؛ الأرخص والأصدق هو الأول + إسقاط/إعادة تعريف فئة Spawn Outpost مؤقتاً).
  3. استدعاء `isViableStructureTerrain()` للأنواع المتأثرة + إضافة حقل `viability:"biome-check-only"` لكل structure + تخفيف صياغة كل explanations إلى وقائع هندسية.
  4. توحيد قاعدة الـ seed بين CLI وFrontend (مكان واحد + اختبارات).
  5. إضافة `"schema_version": 1` للـ JSON.
- **السبب:** كل ما بعده (scoring/UX/scale) بلا قيمة فوق بيانات مضللة.
- **المخاطر:** تغيير أسماء الحقول يكسر الـ frontend — يُنسَّق مع تحديث `seedlens.ts` المتزامن. إعادة تعريف Spawn Outpost تُبطل regression الحالية (مقصود — تُحدَّث الاختبارات لتعكس الحقيقة).
- **الاختبار:** اختبارات الدخل الفاسد (6 حالات)، مقارنة spawn الحقيقي/الأصل على عينة 50 seed، فحص أن `isViableStructureTerrain` يرفض ≥1 حالة pyramid حقيقية (إثبات الفائدة)، `diff` حتمية.

### Phase 2 — خوارزميات التجميع والتصنيف (P1)

- **الهدف:** إزالة الفقد الصامت وتحيز الحواف، واستبدال magic numbers بقواعد مُشتقة.
- **الملفات:** `engine/src/{cluster,analysis}.c`، `internal.h`، `seedlens.h`، `tests/test_regression.py` (+ وحدات C جديدة).
- **التغيير:**
  1. فصل `total_candidates` عن `returned_clusters` في الـ JSON + سياسة قطع معلنة (ترتيب بـ usefulness لا بـ type_count) أو pagination.
  2. وسم عناقيد الحافة `edge_truncated:true`.
  3. استبدال dedup (250م/60%) بقاعدة مثبتة (Jaccard + مسافة، مُعايرة على عينة)؛ استبدال المركز المتوسط بـ medoid.
  4. تجميد v3.1 كـ baseline، وبناء harness معايرة (عينة seeds + تسمية usefulness بشرية/vanilla) قبل أي تعديل عتبة — مع قاعدة "كل تعديل عتبة يتطلب 4-tuple: مشكلة/دليل/تأثير/tests".
- **المخاطر:** تغيير العناقيد يُبطل كل baselines العددية — يُدار كـ `schema_version: 2` مع migration للـ frontend.
- **الاختبار:** وحدات للسلاسل/الحدود/التكرار، اختبار عتبات ±1، عينة معايرة ≥100 seed.

### Phase 3 — البنية متعددة الإصدارات والـ concurrency (P1)

- **الهدف:** دعم 1.21.x لاحقاً + إزالة الـ static state + قابلية النشر.
- **الملفات:** `engine.c`، `internal.h`، `Makefile`، `frontend/src/app/api/analyze/route.ts`، (جديد) `engine/src/version.c`.
- **التغيير:** `VersionDescriptor` table، buffers لكل-استدعاء (إزالة `static`)، `CUBIOMES_DIR` نسبي + pin للإصدار (submodule/hash)، rate-limit + LRU cache + concurrency cap في الـ API، إزالة المسار المطلق `/home/pc/...`.
- **المخاطر:** تغيير توقيع `seedlens_analyze` (كاسر)؛ pin قديم لـ Cubiomes يؤخر ميزات 1.21 — يُقبل عمداً للاستقرار.
- **الاختبار:** اختبار خيوط (TSan إن أمكن)، اختبار تحميل (k6/hey: 10/100 متزامن)، اختبار بناء من clone نظيف.

### Phase 4 — الميزات التوسعية (P2/P3 — مرتبة)

| الأولوية | الميزة | المبرر المختصر |
|---|---|---|
| P1 | Stronghold + spawn الحقيقي + Nether/End الأساسية | بدونها "analysis engine" ناقص جوهرياً لأسئلة اللاعبين الشائعة |
| P1 | Biome الحقيقي لكل structure (من Cubiomes لا من تخمين) | يرفع دقة الفائدة (قرية سهول ≠ قرية ثلجية) ويُغني الخريطة |
| P2 | Loot/progression weighting (shipwreck chest، ancient city، pyramid loot) | القرب وحده ≠ فائدة؛ أول scoring حقيقي |
| P2 | توسيع الهياكل (Outpost/Monument/Mansion/Portal/Trail Ruins) | يقلل التحيز البحري |
| P2 | Cache دائم + بحث حدود قابلة للضبط | يفتح 100 concurrent |
| P3 | تحليل تضاريس/عبور (مسار فعلي لا XZ) | يصحح Abyssal Hub جذرياً لكنه مكلف |
| P3 | Rare combos / scenic / resource proximity | تسويقي أكثر منه جوهري |
| P3 | استبدال خريطة Simplex الوهمية ببلاطات حقيقية أو شارة "stylized" | تنظيف UX مضلل |

---

### ملاحظة أخيرة عن المقارنة مع تقارير AI أخرى

أعلى 5 حقائق قابلة لإعادة الإنتاج آلياً (للمقارنة): (1) overflow الصامت `999…→LLONG_MAX`، (2) `nearest_spawn` = مسافة الأصل (سطر `discovery.c:72`)، (3) صفر استدعاءات لـ `isViableStructureTerrain`، (4) `total_clusters=20` = قيمة السقف، (5) `+123` يُقبل CLI ويُرفض/يُجزّأ Frontend. أي تقرير لا يذكر هذه الخمسة لم يفحص التنفيذ فعلياً.
