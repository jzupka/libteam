/*
 *   teamd_hash_func.c - Hash function preparation for teamd
 *   Copyright (C) 2012 Jiri Pirko <jpirko@redhat.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>
#include <linux/filter.h>
#include <private/misc.h>
#include <team.h>

#include "teamd.h"
#include "teamd_config.h"
#include "teamd_bpf_chef.h"

static const struct teamd_bpf_hash_field eth_hdr_hash_field[] = {
	{ /* First 4 bytes of src addr */
		.offset = 0,
		.type = BPF_W,
	},
	{ /* Last 2 bytes of src addr */
		.offset = 4,
		.type = BPF_H,
	},
	{ /* First 4 bytes of dst addr */
		.offset = 6,
		.type = BPF_W,
	},
	{ /* Last 2 bytes of dst addr */
		.offset = 10,
		.type = BPF_H,
	},
};

static const struct teamd_bpf_desc_frag eth_hdr_frag = {
	.name = "eth",
	.hash_field = eth_hdr_hash_field,
	.hash_field_count = ARRAY_SIZE(eth_hdr_hash_field),
};

static const struct teamd_bpf_pattern ipv4_hdr_pattern[] = {
	{ /* type IPv4 */
		.offset = 12,
		.type = BPF_H,
		.value = 0x0800,
	},
};

static const struct teamd_bpf_hash_field ipv4_hdr_hash_field[] = {
	{ /* 4 bytes of src addr */
		.offset = 26,
		.type = BPF_W,
	},
	{ /* 4 bytes of dst addr */
		.offset = 30,
		.type = BPF_W,
	},
};

static const struct teamd_bpf_desc_frag ipv4_hdr_frag = {
	.name = "ipv4",
	.pattern = ipv4_hdr_pattern,
	.pattern_count = ARRAY_SIZE(ipv4_hdr_pattern),
	.hash_field = ipv4_hdr_hash_field,
	.hash_field_count = ARRAY_SIZE(ipv4_hdr_hash_field),
};

static const struct teamd_bpf_pattern ipv6_hdr_pattern[] = {
	{ /* type IPv6 */
		.offset = 12,
		.type = BPF_H,
		.value = 0x86dd,
	},
};

static const struct teamd_bpf_hash_field ipv6_hdr_hash_field[] = {
	{ /* first 4 bytes of src addr */
		.offset = 22,
		.type = BPF_W,
	},
	{ /* second 4 bytes of src addr */
		.offset = 26,
		.type = BPF_W,
	},
	{ /* third 4 bytes of src addr */
		.offset = 30,
		.type = BPF_W,
	},
	{ /* fourth 4 bytes of src addr */
		.offset = 34,
		.type = BPF_W,
	},
	{ /* first 4 bytes of dst addr */
		.offset = 38,
		.type = BPF_W,
	},
	{ /* second 4 bytes of dst addr */
		.offset = 42,
		.type = BPF_W,
	},
	{ /* third 4 bytes of dst addr */
		.offset = 46,
		.type = BPF_W,
	},
	{ /* fourth 4 bytes of dst addr */
		.offset = 50,
		.type = BPF_W,
	},
};

static const struct teamd_bpf_desc_frag ipv6_hdr_frag = {
	.name = "ipv6",
	.pattern = ipv6_hdr_pattern,
	.pattern_count = ARRAY_SIZE(ipv6_hdr_pattern),
	.hash_field = ipv6_hdr_hash_field,
	.hash_field_count = ARRAY_SIZE(ipv6_hdr_hash_field),
};

static const struct teamd_bpf_desc_frag *frags[] = {
	&eth_hdr_frag,
	&ipv4_hdr_frag,
	&ipv6_hdr_frag,
};

static const size_t frags_count = ARRAY_SIZE(frags);

static const struct teamd_bpf_desc_frag *__find_frag(const char *frag_name)
{
	int i;

	for (i = 0; i < frags_count; i++) {
		if (!strcmp(frag_name, frags[i]->name))
			return frags[i];
	}
	return NULL;
}

static int teamd_hash_func_init(struct teamd_context *ctx, struct sock_fprog *fprog)
{
	int i;
	int err;

	teamd_bpf_desc_compile_start(fprog);
	teamd_config_for_each_arr_index(i, ctx, "$.runner.tx_hash") {
		const struct teamd_bpf_desc_frag *frag;
		const char *frag_name;

		err = teamd_config_string_get(ctx, &frag_name,
					      "$.runner.tx_hash[%d]", i);
		if (err)
			continue;

		frag = __find_frag(frag_name);
		if (!frag) {
			teamd_log_warn("Hash frag named \"%s\" not found.",
				       frag_name);
			continue;
		}
		err = teamd_bpf_desc_compile_frag(fprog, frag);
		if (err)
			goto release;
	}
	err = teamd_bpf_desc_compile_finish(fprog);
	if (err)
		goto release;
	return 0;

release:
	teamd_bpf_desc_compile_release(fprog);
	return err;
}

static void teamd_hash_func_fini(struct sock_fprog *fprog)
{
	teamd_bpf_desc_compile_release(fprog);
}

static const char *teamd_hash_default_frags[] = {
	"eth", "ipv4", "ipv6",
};

static int teamd_hash_func_add_default_frags(struct teamd_context *ctx)
{
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(teamd_hash_default_frags); i++) {
		err = teamd_config_arr_string_append(ctx,
						     teamd_hash_default_frags[i],
						     "$.runner.tx_hash");
		if (err)
			return err;
	}
	return 0;
}

int teamd_hash_func_set(struct teamd_context *ctx)
{
	struct sock_fprog fprog;
	int err;

	if (!teamd_config_path_exists(ctx, "$.runner.tx_hash")) {
		teamd_log_dbg("No Tx hash recipe found in config.");
		err = teamd_hash_func_add_default_frags(ctx);
		if (err)
			return err;
	}
	err = teamd_hash_func_init(ctx, &fprog);
	if (err) {
		teamd_log_err("Failed to init hash function.");
		return err;
	}
	err = team_set_bpf_hash_func(ctx->th, &fprog);
	if (err)
		teamd_log_err("Failed to set hash function.");
	teamd_hash_func_fini(&fprog);
	return err;
}
