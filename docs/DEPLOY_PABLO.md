# Οδηγός: Φόρτωση του `moos-ivp-extend` στο PABLO (Raspberry Pi)

Αναλυτικές οδηγίες για να ανεβάσεις και να χτίσεις το δικό σου `moos-ivp-extend`
(με τα labs: `pGenPath`, `pGenRescue`, `pOdometry`, `pPointAssign`) πάνω στο
φυσικό PABLO box του σκάφους.

> **Τι είναι το PABLO:** ο onboard payload υπολογιστής (Raspberry Pi, αρχιτεκτονική
> `aarch64`/ARM) που κάθεται πάνω στο σκάφος και τρέχει τον MOOSDB + helm.
> Ο `pMarineViewer` **δεν** τρέχει εκεί — αυτός τρέχει στο laptop σου (shoreside).

---

## 0. Προϋποθέσεις (μία φορά)

| Τι | Έλεγχος |
|----|---------|
| Δίκτυο | Είσαι στο **ίδιο wired LAN** ή στο Wi-Fi **`HNA-WF`** με τα PABLO boxes |
| IP του box σου | Δες τον πίνακα IP. Το δικό σου: **G09 → `172.16.0.196`**, χρήστης `pabuser` |
| Κωδικός `pabuser` | `aquabotz` (χρειάζεται **μόνο** την πρώτη φορά, για το SSH key) |
| Κώδικας στο GitHub | Ό,τι θες να ανεβάσεις πρέπει να είναι **committed & pushed**: `git push origin main` |

### Πίνακας IP (αναφορά)

```
Box   IP              Box   IP
G01   172.16.0.254    G11   172.16.0.243
G02   172.16.1.107    G12   172.16.1.15
G03   172.16.1.0      G13   172.16.1.63
G04   172.16.1.2      G15   172.16.1.102
G07   172.16.0.181    G16   172.16.0.232
G08   172.16.0.253    G17   172.16.1.7
G09   172.16.0.196    G18   172.16.0.235  ← δικό μου: G09
G10   172.16.1.65     G20   172.16.0.237
```

> Σε όλο τον οδηγό χρησιμοποιώ το **G09 = `172.16.0.196`**. Αν αλλάξεις box,
> άλλαξε μόνο το IP.

---

## 1. Έλεγχος συνδεσιμότητας

Πρώτα βεβαιώσου ότι «βλέπεις» το box:

```bash
ping -c 3 172.16.0.196
```

Αν παίρνεις απαντήσεις (π.χ. `64 bytes from 172.16.0.196 ... time=7ms`), είσαι στο
σωστό δίκτυο. Αν όχι → δες το [Troubleshooting](#troubleshooting).

---

## 2. SSH key (μία φορά — μετά δεν ξαναζητάει κωδικό)

Για να μην πληκτρολογείς κωδικό σε κάθε εντολή, αντίγραψε το δημόσιο κλειδί σου
στο PABLO **μία φορά**:

```bash
# Αν δεν έχεις κλειδί, φτιάξε ένα (πάτα Enter σε όλα):
ls ~/.ssh/id_ed25519.pub || ssh-keygen -t ed25519

# Αντίγραψέ το στο PABLO (θα ζητήσει τον κωδικό: aquabotz)
ssh-copy-id -i ~/.ssh/id_ed25519.pub pabuser@172.16.0.196
```

**Δοκιμή** ότι δουλεύει χωρίς κωδικό:

```bash
ssh pabuser@172.16.0.196 'echo OK; hostname; uname -m'
```

Περιμένεις: `OK`, το hostname (π.χ. `pisource`) και `aarch64`.

---

## 3. Πρώτη εγκατάσταση (clone + build)

> Κάνε αυτό **μόνο την πρώτη φορά**. Για ενημερώσεις αργότερα, δες την §4.

Το PABLO έχει **ήδη χτισμένο** το core `moos-ivp` στο `/home/pabuser/moos-ivp`.
Εμείς βάζουμε **μόνο** το δικό σου repo **δίπλα** του, ώστε το CMake να το βρει
αυτόματα.

### 3α. Clone από το GitHub

```bash
ssh pabuser@172.16.0.196 \
  'git clone https://github.com/kiriakosal2017-dot/moos-ivp-extend.git ~/moos-ivp-extend'
```

> Χρησιμοποιούμε **HTTPS** (το repo είναι public) ώστε να μη χρειάζεται να βάλουμε
> προσωπικό GitHub key στο κοινόχρηστο PABLO.

### 3β. Build (compile για ARM)

```bash
ssh pabuser@172.16.0.196 'cd ~/moos-ivp-extend && ./build.sh'
```

Παίρνει μερικά λεπτά. Στο τέλος πρέπει να δεις `[100%] Built target pGenRescue`.
Το CMake θα τυπώσει κάτι σαν:
`-- Found MOOS: /home/pabuser/moos-ivp/build/MOOS/MOOSCore/...` — αυτό σημαίνει ότι
βρήκε σωστά το core moos-ivp.

### 3γ. Σύνδεση στο PATH / IVP_BEHAVIOR_DIRS

Για να τρέχουν τα apps σου με το όνομά τους (και κατά την εκκίνηση αποστολών),
πρόσθεσε **μία φορά** ένα μπλοκ στο `~/.bashrc` του PABLO.

> ⚠️ **Κρίσιμο σημείο:** το `~/.bashrc` του PABLO έχει έναν «φρουρό» που σταματάει
> νωρίς σε *non-interactive* shells:
> ```bash
> case $- in *i*) ;; *) return;; esac
> ```
> Οι αποστολές συχνά εκκινούνται non-interactive (π.χ. το shoreside στέλνει
> `ssh pablo "./launch.sh"`). Γι' αυτό το μπλοκ μας πρέπει να μπει **ΠΡΙΝ** αυτόν
> τον φρουρό — αλλιώς δεν θα φορτώνεται στις αποστολές.

Τρέξε **όλο** το παρακάτω (αντιγράφει backup, μπαίνει στη σωστή θέση, και
αποφεύγει διπλοεγγραφές):

```bash
ssh pabuser@172.16.0.196 'bash -s' <<'REMOTE'
set -e
# Αν υπάρχει ήδη, μην το ξαναβάλεις
if grep -q "moos-ivp repo: moos-ivp-extend" ~/.bashrc; then
  echo "Block already present — skipping."
  exit 0
fi
cp ~/.bashrc ~/.bashrc.bak.preextend
cat > /tmp/extend_block.txt <<'EOF'

# >>> moos-ivp repo: moos-ivp-extend >>>
PATH="$PATH:/home/pabuser/moos-ivp-extend/bin"
PATH="$PATH:/home/pabuser/moos-ivp-extend/scripts"
IVP_BEHAVIOR_DIRS="$IVP_BEHAVIOR_DIRS:/home/pabuser/moos-ivp-extend/lib"
export PATH
export IVP_BEHAVIOR_DIRS
# <<< moos-ivp repo: moos-ivp-extend <<<
EOF
# Μπαίνει αμέσως μετά τη γραμμή pablo-common/bin (που είναι ΠΡΙΝ τον φρουρό)
sed -i "/pablo-common\/bin$/r /tmp/extend_block.txt" ~/.bashrc
rm -f /tmp/extend_block.txt
echo "Done. Backup at ~/.bashrc.bak.preextend"
REMOTE
```

---

## 4. Ενημέρωση αργότερα (κάθε φορά που αλλάζεις κώδικα)

Αφού κάνεις τις αλλαγές σου τοπικά:

```bash
# Τοπικά (στο laptop σου), στον φάκελο του repo:
git add -A && git commit -m "..." && git push origin main
```

Μετά, ανέβασέ τες στο PABLO με μία εντολή:

```bash
ssh pabuser@172.16.0.196 'cd ~/moos-ivp-extend && git pull && ./build.sh'
```

> Το PATH/`.bashrc` το βάζεις **μόνο μία φορά** (§3γ) — δεν χρειάζεται ξανά.

---

## 5. Επαλήθευση ότι όλα δουλεύουν

```bash
ssh pabuser@172.16.0.196 'bash -lc "
  for a in pGenRescue pGenPath pPointAssign pOdometry; do
    printf \"%-12s \" \$a; command -v \$a || echo MISSING;
  done
  echo \$IVP_BEHAVIOR_DIRS | tr : \"\n\" | grep extend
"'
```

Περιμένεις κάθε app να δείχνει σε `/home/pabuser/moos-ivp-extend/bin/...` και να
εμφανίζεται το `.../moos-ivp-extend/lib` στο `IVP_BEHAVIOR_DIRS`.

---

## 6. (Επόμενο βήμα) Εκτέλεση αποστολής

- Ο **MOOSDB + helm** τρέχουν στο PABLO (μέσω `pAntler` της αποστολής).
- Ο **`pMarineViewer`** τρέχει στο **laptop σου** (shoreside) και συνδέεται στο
  PABLO μέσω `pShare`/`uFldShoreBroker`.
- Τα missions είναι ήδη στο `/home/pabuser/moos-ivp-extend/missions/`.

> Αυτό το κομμάτι (shoreside ↔ vehicle σύνδεση, IP/ports) είναι ξεχωριστή
> διαδικασία — δες τα launch scripts της κάθε αποστολής.

---

## Troubleshooting

| Σύμπτωμα | Αιτία / Λύση |
|----------|--------------|
| `ping` δεν απαντάει | Δεν είσαι στο σωστό δίκτυο. Σύνδεση σε wired LAN ή Wi-Fi `HNA-WF`. |
| SSH ζητάει κωδικό κάθε φορά | Το key δεν πέρασε. Ξανατρέξε §2 (`ssh-copy-id`). |
| `Permission denied (publickey,password)` | Λάθος χρήστης/κωδικός. Χρήστης = `pabuser`, κωδικός = `aquabotz`. |
| `Found MOOS: ... NOT found` στο build | Το repo δεν είναι **δίπλα** στο `~/moos-ivp`. Πρέπει να είναι στο `~/moos-ivp-extend`. |
| App εμφανίζεται `MISSING` σε αποστολή (non-interactive) | Το `.bashrc` block μπήκε **μετά** τον φρουρό. Δες §3γ — πρέπει να είναι ΠΡΙΝ το `case $- in ... return`. |
| Θέλω να αναιρέσω το `.bashrc` | `ssh pabuser@172.16.0.196 'cp ~/.bashrc.bak.preextend ~/.bashrc'` |
| `git pull` λέει "local changes" | Στο PABLO δεν επεξεργαζόμαστε κώδικα. `cd ~/moos-ivp-extend && git checkout -- . && git pull` |

---

## Σύνοψη εντολών (cheat sheet)

```bash
# 1) Key (μία φορά)
ssh-copy-id -i ~/.ssh/id_ed25519.pub pabuser@172.16.0.196

# 2) Πρώτη εγκατάσταση
ssh pabuser@172.16.0.196 'git clone https://github.com/kiriakosal2017-dot/moos-ivp-extend.git ~/moos-ivp-extend'
ssh pabuser@172.16.0.196 'cd ~/moos-ivp-extend && ./build.sh'
# + το .bashrc block της §3γ (μία φορά)

# 3) Ενημέρωση (κάθε φορά)
git push origin main                                                   # τοπικά
ssh pabuser@172.16.0.196 'cd ~/moos-ivp-extend && git pull && ./build.sh'
```
