package rtmmcp

import (
	"context"
	"errors"
	"strings"
	"testing"

	"destiny-rtm/internal/rtmclient"
)

type actorDescriptorClient struct {
	fakeClient
	snapshot rtmclient.ActorPoolDescriptorSnapshot
	err      error
	calls    int
	target   string
}

func (f *actorDescriptorClient) ReadActorPoolDescriptor(ctx context.Context, target string) (rtmclient.ActorPoolDescriptorSnapshot, error) {
	f.calls++
	f.target = target
	if err := ctx.Err(); err != nil {
		return rtmclient.ActorPoolDescriptorSnapshot{}, err
	}
	return f.snapshot, f.err
}

func TestActorPoolToolReportsDescriptorWithoutClaimingEnemyEnumeration(t *testing.T) {
	client := &actorDescriptorClient{snapshot: rtmclient.ActorPoolDescriptorSnapshot{
		Profile: "BLES01857 01.24 signature profile", DescriptorAddress: "0x01A1E160",
		StoragePointer: "0x10000000", Stride: 0x28D0, Capacity: 40, BackingBytes: 0x66080,
		Initialized: true, Atomic: false, RecordsRead: 0,
	}}
	responses := runServer(t, client, toolRequest(1, "rtm_read_actor_pool_descriptor", `{"target_ipv4":"192.168.0.62"}`))
	result := structuredObject(t, responseByID(t, responses, 1))
	if result["ok"] != true || result["read_only"] != true || result["snapshot_atomic"] != false ||
		result["loaded_enemy_list_confirmed"] != false || result["spawnability_confirmed"] != false {
		t.Fatalf("descriptor inspection overstated its result: %#v", result)
	}
	snapshot := result["snapshot"].(map[string]any)
	if snapshot["capacity"] != float64(40) || snapshot["stride"] != float64(0x28D0) || snapshot["records_read"] != float64(0) {
		t.Fatalf("unexpected descriptor result: %#v", snapshot)
	}
	if client.calls != 1 || client.target != "192.168.0.62" || client.reads != 0 || len(client.writes) != 0 {
		t.Fatalf("tool bypassed the bounded descriptor reader: %#v", client)
	}
}

func TestActorPoolToolRefusesInvalidTargetsAndExtraReadArguments(t *testing.T) {
	for _, args := range []string{
		`{"target_ipv4":"8.8.8.8"}`,
		`{"target_ipv4":"127.0.0.1"}`,
		`{"target_ipv4":"192.168.0.62","address":"10000000"}`,
		`{"target_ipv4":"192.168.0.62","length":65536}`,
	} {
		client := &actorDescriptorClient{}
		responses := runServer(t, client, toolRequest(1, "rtm_read_actor_pool_descriptor", args))
		if resultObject(t, responseByID(t, responses, 1))["isError"] != true || client.calls != 0 || client.reads != 0 || len(client.writes) != 0 {
			t.Fatalf("invalid arguments reached the client: %s", args)
		}
	}
}

func TestActorPoolToolPropagatesRefusalWithoutPartialSnapshot(t *testing.T) {
	client := &actorDescriptorClient{err: errors.New("actor-pool descriptor changed during sample")}
	responses := runServer(t, client, toolRequest(1, "rtm_read_actor_pool_descriptor", `{"target_ipv4":"192.168.0.62"}`))
	response := responseByID(t, responses, 1)
	result := structuredObject(t, response)
	if resultObject(t, response)["isError"] != true || result["ok"] != false || result["snapshot"] != nil || !strings.Contains(result["error"].(string), "descriptor changed") {
		t.Fatalf("reader refusal was not preserved: %#v", result)
	}
}

func TestActorPoolToolRequiresDedicatedReader(t *testing.T) {
	client := &fakeClient{}
	responses := runServer(t, client, toolRequest(1, "rtm_read_actor_pool_descriptor", `{"target_ipv4":"192.168.0.62"}`))
	response := responseByID(t, responses, 1)
	if resultObject(t, response)["isError"] != true || client.reads != 0 || len(client.writes) != 0 {
		t.Fatal("unsupported client triggered a raw-memory fallback")
	}
}

func TestActorPoolToolIsAdvertisedAsReadOnly(t *testing.T) {
	for _, descriptor := range toolList {
		if descriptor.Name != "rtm_read_actor_pool_descriptor" {
			continue
		}
		if descriptor.Annotations["readOnlyHint"] != true || descriptor.Annotations["destructiveHint"] != false {
			t.Fatalf("incorrect descriptor tool annotations: %#v", descriptor.Annotations)
		}
		return
	}
	t.Fatal("actor-pool descriptor tool is not advertised")
}
