"""Friends over the fake brokers (port/linux/src/social_friends.c, social_store.c, social_front.c; tests/social_friends_check.c): requests, answers, the exchange of
circle keys, removing and blocking with the key changed, the file (its damage, its power losses, a newer version), the saver thread that never makes the engine's thread
wait, restarts and a restored card, and the threads of the game calling the API at once. Each negative control breaks a source and shows a named case fail."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import social

CASES = ["befriend", "crossed", "decline", "stranger-answers", "remove-and-rotate", "block", "rotation-offline", "list-cap", "off-by-default", "request-flood",
         "store-roundtrip", "store-hostile", "store-version", "store-power-loss", "store-short-write", "saver-thread", "restart", "restored-card", "front-threads"]


@pytest.fixture(scope="module")
def driver():
    return social.build("social_friends_check")


@pytest.mark.parametrize("case", CASES)
def test_case(driver, case):
    status, output, errors = social.run(driver, case)
    assert status == 0, errors + output


FAULTS = [
    # a hello that is not answered: the keys go one way only
    ("befriend", "social_friends.c", "	if (new_circle || !friend->acknowledged)\n		friend->reply_valid = 1;", "	(void)new_circle;"),
    # the stranger who answers passes for the one whose code was typed
    ("stranger-answers", "social_friends.c", "	if (matched >= 0)\n	{\n		/* the answer to a request of ours", "	if (matched >= 0 || engine->sent[0].used)\n	{\n		/* the answer to a request of ours"),
    # removing a friend does not change the key
    ("remove-and-rotate", "social_friends.c", "	social_topics_changed(engine);\n	rotate_circle(engine);\n	return 1;\n}\n\nint social_engine_block", "	social_topics_changed(engine);\n	return 1;\n}\n\nint social_engine_block"),
    # removing a friend does not tell it
    ("remove-and-rotate", "social_friends.c", "	send_remove(engine, friend, 0);\n	social_log(engine, \"removed %s\"", "	social_log(engine, \"removed %s\""),
    # a blocked key's requests are read
    ("block", "social_friends.c", "	if (listed((const unsigned char (*)[SOCIAL_BLOCK_SIZE])engine->blocked, engine->blocked_count, hash) ||\n		listed(", "	if (listed("),
    # a request turned away is asked about again
    ("decline", "social_friends.c", "		memcpy(engine->ignored[engine->ignored_next], hash, SOCIAL_BLOCK_SIZE);", "		(void)hash;"),
    # a friend that was away is not told the new key again
    ("rotation-offline", "social_friends.c", "		friend->hello_next = social_now(engine) + delay;\n", "		friend->hello_next = social_now(engine) + delay;\n		friend->acknowledged = 1;\n"),
    # the API says it works when it is off
    ("off-by-default", "social_front.c", "	if (!front->snapshot_made || !front->snapshot.enabled)", "	if (0)"),
    # the file's check not looked at
    ("store-hostile", "social_store.c", "		if (strncmp(start + 6, expected, 16))\n			return 2;", "		(void)expected;"),
    # a newer version read as ours
    ("store-version", "social_store.c", "	if (number > SOCIAL_STORE_VERSION)\n		return 3;", "	(void)number;"),
    # the blocked list not capped when read
    ("store-hostile", "social_store.c", "engine->blocked_count < SOCIAL_MAX_BLOCKED && ", ""),
    # only friends.txt tried, never friends.new or friends.bak
    ("store-power-loss", "social_store.c", "	for (index = 0; index < 3; index++)\n	{\n		int size = files->read", "	for (index = 0; index < 1; index++)\n	{\n		int size = files->read"),
    # no read-back before the old file is moved
    ("store-short-write", "social_store.c", "	if (read_size != size || memcmp(readback, text, (size_t)size))", "	if (read_size < 0)"),
    # the saver thread is not used: the card is written on the engine's thread
    ("saver-thread", "social_front.c", "		if (front->options.saver_thread && pthread_create(&front->thread, NULL, saver_thread, front) == 0)", "		if (0 && pthread_create(&front->thread, NULL, saver_thread, front) == 0)"),
    # the generation chosen is not kept
    ("restart", "social_core.c", "	engine->stored_generation = engine->generation;\n	engine->generation_ready = 1;", "	engine->generation_ready = 1;"),
    # the circle key is not kept
    ("restart", "social_store.c", "\"circle_gen %d\\ncircle_key %s\\n\"", "\"circle_gen %d\\ncircle_keyx %s\\n\""),
    # a restored card's generation falls below what was sent
    ("restored-card", "social_core.c", "	engine->generation = engine->stored_generation + 1 > minutes ? engine->stored_generation + 1 : minutes;", "	engine->generation = engine->stored_generation + 1;\n	(void)minutes;"),
    # the request list not bounded by the budget of signatures
    ("request-flood", "social_core.c", "	if (engine->verify_tokens <= 0)\n		return 0;\n	engine->verify_tokens--;\n	return 1;", "	return 1;"),
    # the open rate gone
    ("request-flood", "social_core.c", "	return ++engine->open_count <= SOCIAL_OPEN_RATE;", "	return 1;"),
]


@pytest.mark.parametrize("case,source,before,after", FAULTS, ids=[f"{f[0]}-{i}" for i, f in enumerate(FAULTS)])
def test_negative_control(case, source, before, after):
    executable = social.build("social_friends_check", mutations=((source, before, after),))
    status, output, errors = social.run(executable, case)
    assert status != 0, f"the fault was not caught by {case}"
